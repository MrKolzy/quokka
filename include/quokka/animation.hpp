#pragma once

#include <glm/glm.hpp>

#include <cstdint> // std::uint32_t
#include <vector> // std::vector

namespace quokka
{
	struct Skeleton;

	// Must match the clip constants in shader.slang
	enum class AnimationClip : std::uint32_t
	{
		idle,
		walk,
		run,
		attack,
		death,
		jump,
		land,
		count
	};

	struct ClipInfo
	{
		// Must match ClipInfo in shader.slang
		// Where the clip's first frame starts among the joint matrices
		std::uint32_t first_matrix;
		std::uint32_t frame_count;
		std::uint32_t joint_count;

		// In seconds
		float duration;

		// 0 or 1, the others hold their last frame once they end
		std::uint32_t is_looping;
	};

	struct BakedAnimations
	{
		// Every frame of every clip, each one a matrix per joint, which is the joint's transform
		// in the model's space times its inverse bind matrix, so it moves a vertex from the bind
		// pose to the animated one
		std::vector<glm::mat4> joint_matrices {};

		// Indexed by AnimationClip
		std::vector<ClipInfo> clips {};
	};

	// Frames per second the clips are sampled at, often enough that interpolating between
	// two frames looks like the original curves
	inline constexpr float animation_frame_rate { 30.0f };

	// Samples the clips of the rig files for this skeleton, matching their joints by name,
	// so it works for any model made for the same rig
	[[nodiscard]] BakedAnimations bake_animations(const Skeleton& skeleton);
}
