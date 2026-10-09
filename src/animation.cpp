#include "quokka/animation.hpp"

#include "quokka/constants.hpp"
#include "quokka/gltf.hpp"
#include "quokka/mesh.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm> // std::max, std::ranges::upper_bound
#include <array> // std::array
#include <cmath> // std::round
#include <cstddef> // std::size_t
#include <map> // std::map
#include <optional> // std::optional
#include <stdexcept> // std::runtime_error
#include <string> // std::string
#include <unordered_map> // std::unordered_map
#include <utility> // std::move

namespace quokka
{
	namespace
	{
		// Where each clip comes from, and whether it repeats or plays once
		struct ClipSource
		{
			AnimationClip clip;
			const char* file;
			const char* name;
			bool is_looping;
		};

		// The free KayKit packs have no melee attack, so Throw's arm swing stands in for one
		constexpr std::array clip_sources {
			ClipSource { AnimationClip::idle, constants::general_rig_path, "Idle_A", true },
			ClipSource { AnimationClip::walk, constants::movement_rig_path, "Walking_A", true },
			ClipSource { AnimationClip::run, constants::movement_rig_path, "Running_A", true },
			ClipSource { AnimationClip::attack, constants::general_rig_path, "Throw", false },
			ClipSource { AnimationClip::death, constants::general_rig_path, "Death_A", false }
		};

		// In AnimationClip order, so a clip's index is its place in the buffer
		[[nodiscard]] constexpr bool are_sources_in_clip_order() noexcept
		{
			for (std::size_t index { 0 }; index < clip_sources.size(); ++index)
			{
				if (static_cast<std::size_t>(clip_sources[index].clip) != index)
				{
					return false;
				}
			}

			return clip_sources.size() == static_cast<std::size_t>(AnimationClip::count);
		}

		static_assert(are_sources_in_clip_order());

		// One animated property of one joint, translation and scale use 3 of the 4 components
		struct Curve
		{
			std::vector<float> times {};
			std::vector<glm::vec4> values {};
		};

		[[nodiscard]] Curve read_curve(
			const tinygltf::Model& model,
			const tinygltf::AnimationSampler& sampler,
			bool is_rotation)
		{
			// Cubic splines would also need their tangents, which the rigs used don't have
			if (sampler.interpolation != "LINEAR")
			{
				throw std::runtime_error("Unsupported animation interpolation in the rig file");
			}

			const auto& input_accessor { at_index(model.accessors, sampler.input) };
			const auto& output_accessor { at_index(model.accessors, sampler.output) };
			const int value_type { is_rotation ? TINYGLTF_TYPE_VEC4 : TINYGLTF_TYPE_VEC3 };
			if (input_accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
				output_accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
				output_accessor.type != value_type ||
				input_accessor.count != output_accessor.count)
			{
				throw std::runtime_error("Unsupported animation data in the rig file");
			}

			const auto times { accessor_data(model, input_accessor, sizeof(float)) };
			const std::size_t value_size { is_rotation ? sizeof(glm::vec4) : sizeof(glm::vec3) };
			const auto values { accessor_data(model, output_accessor, value_size) };

			Curve curve {};
			for (std::size_t index { 0 }; index < times.count; ++index)
			{
				curve.times.push_back(read_element<float>(times, index));
				curve.values.push_back(is_rotation
					? read_element<glm::vec4>(values, index)
					: glm::vec4 { read_element<glm::vec3>(values, index), 0.0f });
			}

			return curve;
		}

		// The two keys around the time and how far between them it is, held before the first
		// key and after the last
		struct KeyPair
		{
			std::size_t before;
			std::size_t after;
			float fraction;
		};

		[[nodiscard]] KeyPair find_keys(const Curve& curve, float time)
		{
			const auto next { std::ranges::upper_bound(curve.times, time) };
			if (next == curve.times.begin())
			{
				return { 0, 0, 0.0f };
			}
			if (next == curve.times.end())
			{
				const std::size_t last { curve.times.size() - 1 };

				return { last, last, 0.0f };
			}

			const auto after { static_cast<std::size_t>(next - curve.times.begin()) };
			const std::size_t before { after - 1 };
			const float span { curve.times[after] - curve.times[before] };

			return { before, after, span > 0.0f ? (time - curve.times[before]) / span : 0.0f };
		}

		[[nodiscard]] glm::vec3 sample_vector(const Curve& curve, float time)
		{
			const KeyPair keys { find_keys(curve, time) };

			return glm::mix(
				glm::vec3 { curve.values[keys.before] },
				glm::vec3 { curve.values[keys.after] },
				keys.fraction);
		}

		[[nodiscard]] glm::quat sample_rotation(const Curve& curve, float time)
		{
			const KeyPair keys { find_keys(curve, time) };

			// glTF stores x, y, z, w, while glm's constructor takes w first
			const auto to_quat = [](const glm::vec4& value)
			{
				return glm::quat { value.w, value.x, value.y, value.z };
			};

			// Spherical, so the rotation keeps a constant speed and the quaternion unit length
			return glm::slerp(
				to_quat(curve.values[keys.before]),
				to_quat(curve.values[keys.after]),
				keys.fraction);
		}

		// The curves of one joint, and the joint keeps its rest value where it has none
		struct JointCurves
		{
			std::optional<Curve> translation {};
			std::optional<Curve> rotation {};
			std::optional<Curve> scale {};
		};

		[[nodiscard]] const tinygltf::Animation& find_animation(
			const tinygltf::Model& model,
			const char* name)
		{
			for (const auto& animation : model.animations)
			{
				if (animation.name == name)
				{
					return animation;
				}
			}

			throw std::runtime_error(
				std::string { "No animation named " } + name + " in the rig file");
		}

		// Global transforms from the local ones, with every parent before its children
		void compute_global_transform(
			const Skeleton& skeleton,
			const std::vector<glm::mat4>& local_transforms,
			std::size_t joint,
			std::vector<std::optional<glm::mat4>>& global_transforms)
		{
			if (global_transforms[joint])
			{
				return;
			}

			const int parent { skeleton.parents[joint] };
			glm::mat4 parent_transform { skeleton.root_transforms[joint] };
			if (parent >= 0)
			{
				const auto parent_index { static_cast<std::size_t>(parent) };
				compute_global_transform(
					skeleton,
					local_transforms,
					parent_index,
					global_transforms);
				parent_transform = *global_transforms[parent_index];
			}

			global_transforms[joint] = parent_transform * local_transforms[joint];
		}

		void bake_clip(
			const Skeleton& skeleton,
			const tinygltf::Model& rig,
			const ClipSource& source,
			BakedAnimations& animations)
		{
			const tinygltf::Animation& animation { find_animation(rig, source.name) };
			const std::size_t joint_count { skeleton.joint_names.size() };

			std::unordered_map<std::string, std::size_t> joints_by_name {};
			for (std::size_t joint { 0 }; joint < joint_count; ++joint)
			{
				joints_by_name.emplace(skeleton.joint_names[joint], joint);
			}

			// Channels of nodes this skeleton doesn't have, like the rig's own meshes, are skipped
			std::vector<JointCurves> curves(joint_count);
			float duration { 0.0f };
			for (const auto& channel : animation.channels)
			{
				const auto& node { at_index(rig.nodes, channel.target_node) };
				const auto joint { joints_by_name.find(node.name) };
				if (joint == joints_by_name.end())
				{
					continue;
				}

				const auto& sampler { at_index(animation.samplers, channel.sampler) };
				const bool is_rotation { channel.target_path == "rotation" };
				Curve curve { read_curve(rig, sampler, is_rotation) };
				duration = std::max(duration, curve.times.back());

				JointCurves& joint_curves { curves[joint->second] };
				if (channel.target_path == "translation")
				{
					joint_curves.translation = std::move(curve);
				}
				else if (is_rotation)
				{
					joint_curves.rotation = std::move(curve);
				}
				else if (channel.target_path == "scale")
				{
					joint_curves.scale = std::move(curve);
				}
			}

			// Both ends included, so a looping clip's last frame matches its first
			const auto frame_count {
				static_cast<std::uint32_t>(std::round(duration * animation_frame_rate)) + 1
			};

			animations.clips.push_back({
				.first_matrix { static_cast<std::uint32_t>(animations.joint_matrices.size()) },
				.frame_count { frame_count },
				.joint_count { static_cast<std::uint32_t>(joint_count) },
				.duration { duration },
				.is_looping { source.is_looping ? 1u : 0u }
			});

			std::vector<glm::mat4> local_transforms(joint_count);
			for (std::uint32_t frame { 0 }; frame < frame_count; ++frame)
			{
				const float time { static_cast<float>(frame) / animation_frame_rate };

				for (std::size_t joint { 0 }; joint < joint_count; ++joint)
				{
					JointTransform transform { skeleton.rest_transforms[joint] };
					const JointCurves& joint_curves { curves[joint] };
					if (joint_curves.translation)
					{
						transform.translation = sample_vector(*joint_curves.translation, time);
					}
					if (joint_curves.rotation)
					{
						transform.rotation = sample_rotation(*joint_curves.rotation, time);
					}
					if (joint_curves.scale)
					{
						transform.scale = sample_vector(*joint_curves.scale, time);
					}
					local_transforms[joint] = to_matrix(transform);
				}

				std::vector<std::optional<glm::mat4>> global_transforms(joint_count);
				for (std::size_t joint { 0 }; joint < joint_count; ++joint)
				{
					compute_global_transform(skeleton, local_transforms, joint, global_transforms);
					animations.joint_matrices.push_back(
						*global_transforms[joint] * skeleton.inverse_bind_matrices[joint]);
				}
			}
		}
	}

	// ***

	BakedAnimations bake_animations(const Skeleton& skeleton)
	{
		// Each rig file is read once, however many clips come from it
		std::map<std::string, tinygltf::Model> rigs {};

		BakedAnimations animations {};
		for (const ClipSource& source : clip_sources)
		{
			auto rig { rigs.find(source.file) };
			if (rig == rigs.end())
			{
				rig = rigs.emplace(source.file, read_gltf(source.file)).first;
			}

			bake_clip(skeleton, rig->second, source, animations);
		}

		return animations;
	}
}
