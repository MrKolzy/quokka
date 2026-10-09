#include "quokka/mesh.hpp"

#include "quokka/gltf.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef> // std::size_t
#include <cstring> // std::memcpy
#include <stdexcept> // std::runtime_error
#include <string> // std::string

namespace quokka
{
	namespace
	{
		// The joints and weights of a primitive, which every vertex must have for skinning
		struct SkinAttributes
		{
			AccessorData joints;
			AccessorData weights;
		};

		[[nodiscard]] SkinAttributes skin_attributes(
			const tinygltf::Model& model,
			const tinygltf::Primitive& primitive,
			std::size_t vertex_count)
		{
			const auto joints_attribute { primitive.attributes.find("JOINTS_0") };
			const auto weights_attribute { primitive.attributes.find("WEIGHTS_0") };
			if (joints_attribute == primitive.attributes.end() ||
				weights_attribute == primitive.attributes.end())
			{
				throw std::runtime_error("No joints or weights in the model file");
			}

			const auto& joints_accessor { at_index(model.accessors, joints_attribute->second) };
			const auto& weights_accessor { at_index(model.accessors, weights_attribute->second) };

			// glTF also allows 16-bit joints and normalized integer weights, which aren't needed
			// for the models the engine uses
			const bool has_supported_attributes {
				joints_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
				joints_accessor.type == TINYGLTF_TYPE_VEC4 &&
				weights_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
				weights_accessor.type == TINYGLTF_TYPE_VEC4 &&
				joints_accessor.count == vertex_count &&
				weights_accessor.count == vertex_count
			};
			if (!has_supported_attributes)
			{
				throw std::runtime_error("Unsupported joints or weights in the model file");
			}

			return {
				accessor_data(model, joints_accessor, sizeof(glm::u8vec4)),
				accessor_data(model, weights_accessor, sizeof(glm::vec4))
			};
		}

		void append_primitive(
			const tinygltf::Model& model,
			const tinygltf::Primitive& primitive,
			std::size_t joint_count,
			Mesh& mesh)
		{
			if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
			{
				throw std::runtime_error("Unsupported primitive mode in the model file");
			}

			const auto position_attribute { primitive.attributes.find("POSITION") };
			const auto normal_attribute { primitive.attributes.find("NORMAL") };
			const auto texture_coordinates_attribute { primitive.attributes.find("TEXCOORD_0") };
			const bool has_required_attributes {
				position_attribute != primitive.attributes.end() &&
				normal_attribute != primitive.attributes.end() &&
				texture_coordinates_attribute != primitive.attributes.end()
			};
			if (!has_required_attributes)
			{
				throw std::runtime_error(
					"No positions, normals or texture coordinates in the model file");
			}

			const auto& position_accessor { at_index(model.accessors, position_attribute->second) };
			const auto& normal_accessor { at_index(model.accessors, normal_attribute->second) };
			const auto& texture_coordinates_accessor {
				at_index(model.accessors, texture_coordinates_attribute->second)
			};

			// Positions and normals are always floats in glTF, texture coordinates usually are too
			const bool has_supported_attributes {
				position_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
				position_accessor.type == TINYGLTF_TYPE_VEC3 &&
				normal_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
				normal_accessor.type == TINYGLTF_TYPE_VEC3 &&
				texture_coordinates_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
				texture_coordinates_accessor.type == TINYGLTF_TYPE_VEC2 &&
				position_accessor.count == normal_accessor.count &&
				position_accessor.count == texture_coordinates_accessor.count
			};
			if (!has_supported_attributes)
			{
				throw std::runtime_error("Unsupported vertex attributes in the model file");
			}

			const auto positions { accessor_data(model, position_accessor, sizeof(glm::vec3)) };
			const auto normals { accessor_data(model, normal_accessor, sizeof(glm::vec3)) };
			const auto texture_coordinates {
				accessor_data(model, texture_coordinates_accessor, sizeof(glm::vec2))
			};
			const SkinAttributes skin { skin_attributes(model, primitive, positions.count) };

			// Indices of each primitive start at 0, so they're shifted past the earlier vertices
			const auto first_vertex { static_cast<std::uint32_t>(mesh.vertices.size()) };

			for (std::size_t index { 0 }; index < positions.count; ++index)
			{
				const Vertex vertex {
					.position { read_element<glm::vec3>(positions, index) },
					.normal { read_element<glm::vec3>(normals, index) },
					.texture_coordinates { read_element<glm::vec2>(texture_coordinates, index) },
					.joints { read_element<glm::u8vec4>(skin.joints, index) },
					.weights { read_element<glm::vec4>(skin.weights, index) }
				};

				// Otherwise the vertex shader would read past this model's joints
				for (int component { 0 }; component < 4; ++component)
				{
					if (static_cast<std::size_t>(vertex.joints[component]) >= joint_count)
					{
						throw std::runtime_error("Joint index out of range in the model file");
					}
				}

				mesh.vertices.push_back(vertex);
			}

			// Without indices, every three vertices form a triangle in the order they're stored
			if (primitive.indices < 0)
			{
				for (std::uint32_t index { 0 }; index < positions.count; ++index)
				{
					mesh.indices.push_back(first_vertex + index);
				}

				return;
			}

			const auto& index_accessor { at_index(model.accessors, primitive.indices) };

			// glTF allows 8, 16 and 32-bit unsigned indices
			const bool has_supported_indices {
				index_accessor.type == TINYGLTF_TYPE_SCALAR &&
				(index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
					index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
					index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
			};
			if (!has_supported_indices)
			{
				throw std::runtime_error("Unsupported index type in the model file");
			}

			const auto component_type { static_cast<std::uint32_t>(index_accessor.componentType) };
			const auto index_size {
				static_cast<std::size_t>(tinygltf::GetComponentSizeInBytes(component_type))
			};
			const auto indices { accessor_data(model, index_accessor, index_size) };

			for (std::size_t index { 0 }; index < indices.count; ++index)
			{
				// Little endian like x86, so 1 or 2 bytes copied into a zeroed value keep it intact
				std::uint32_t vertex_index { 0 };
				std::memcpy(
					&vertex_index,
					indices.first_element + index * indices.stride,
					index_size);
				// Otherwise the GPU would read past the end of the vertex buffer
				if (vertex_index >= positions.count)
				{
					throw std::runtime_error("Vertex index out of range in the model file");
				}
				mesh.indices.push_back(first_vertex + vertex_index);
			}
		}

		// glTF stores doubles, and leaves out the parts that keep their default
		[[nodiscard]] JointTransform node_transform(const tinygltf::Node& node)
		{
			// Joints in a matrix would need it split into its parts, which no rig used needs
			if (!node.matrix.empty())
			{
				throw std::runtime_error("Unsupported joint matrix in the model file");
			}

			JointTransform transform {};
			if (node.translation.size() == 3)
			{
				transform.translation = glm::vec3 { glm::make_vec3(node.translation.data()) };
			}
			if (node.rotation.size() == 4)
			{
				// glTF stores x, y, z, w, while glm's constructor takes w first
				transform.rotation = glm::quat {
					static_cast<float>(node.rotation[3]),
					static_cast<float>(node.rotation[0]),
					static_cast<float>(node.rotation[1]),
					static_cast<float>(node.rotation[2])
				};
			}
			if (node.scale.size() == 3)
			{
				transform.scale = glm::vec3 { glm::make_vec3(node.scale.data()) };
			}

			return transform;
		}

		[[nodiscard]] glm::mat4 node_matrix(const tinygltf::Node& node)
		{
			if (node.matrix.size() == 16)
			{
				// Column major in glTF, like glm
				return glm::mat4 { glm::make_mat4(node.matrix.data()) };
			}

			return to_matrix(node_transform(node));
		}

		[[nodiscard]] Skeleton load_skeleton(const tinygltf::Model& model)
		{
			if (model.skins.size() != 1)
			{
				throw std::runtime_error("The model file must have exactly one skin");
			}

			const tinygltf::Skin& skin { model.skins.front() };
			const std::size_t joint_count { skin.joints.size() };

			// glTF only lists children, so the parents are found from them
			std::vector<int> node_parents(model.nodes.size(), -1);
			for (std::size_t node { 0 }; node < model.nodes.size(); ++node)
			{
				for (const int child : model.nodes[node].children)
				{
					node_parents.at(static_cast<std::size_t>(child)) = static_cast<int>(node);
				}
			}

			// Where in the skin each node is, or -1 for nodes that aren't joints
			std::vector<int> node_joints(model.nodes.size(), -1);
			for (std::size_t joint { 0 }; joint < joint_count; ++joint)
			{
				const auto node { static_cast<std::size_t>(skin.joints[joint]) };
				node_joints.at(node) = static_cast<int>(joint);
			}

			Skeleton skeleton {};
			for (const int node_index : skin.joints)
			{
				const tinygltf::Node& node { at_index(model.nodes, node_index) };
				skeleton.joint_names.push_back(node.name);
				skeleton.rest_transforms.push_back(node_transform(node));

				const int parent_node { node_parents.at(static_cast<std::size_t>(node_index)) };
				const int parent_joint {
					parent_node < 0 ? -1 : node_joints.at(static_cast<std::size_t>(parent_node))
				};
				skeleton.parents.push_back(parent_joint);

				// The nodes above a root joint, multiplied from the scene's root down
				glm::mat4 root_transform { 1.0f };
				if (parent_joint < 0)
				{
					for (int ancestor { parent_node }; ancestor >= 0;
						ancestor = node_parents.at(static_cast<std::size_t>(ancestor)))
					{
						const tinygltf::Node& ancestor_node { at_index(model.nodes, ancestor) };
						root_transform = node_matrix(ancestor_node) * root_transform;
					}
				}
				skeleton.root_transforms.push_back(root_transform);
			}

			// Without them, glTF says each one is the identity
			skeleton.inverse_bind_matrices.assign(joint_count, glm::mat4 { 1.0f });
			if (skin.inverseBindMatrices >= 0)
			{
				const auto& accessor { at_index(model.accessors, skin.inverseBindMatrices) };
				if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
					accessor.type != TINYGLTF_TYPE_MAT4 || accessor.count != joint_count)
				{
					throw std::runtime_error("Unsupported inverse bind matrices in the model file");
				}

				const auto matrices { accessor_data(model, accessor, sizeof(glm::mat4)) };
				for (std::size_t joint { 0 }; joint < joint_count; ++joint)
				{
					skeleton.inverse_bind_matrices[joint] =
						read_element<glm::mat4>(matrices, joint);
				}
			}

			return skeleton;
		}
	}

	// ***

	Mesh load_gltf(const std::filesystem::path& path)
	{
		const tinygltf::Model model { read_gltf(path) };

		Mesh mesh {};
		mesh.skeleton = load_skeleton(model);

		for (const auto& gltf_mesh : model.meshes)
		{
			for (const auto& primitive : gltf_mesh.primitives)
			{
				append_primitive(model, primitive, mesh.skeleton.joint_names.size(), mesh);
			}
		}

		return mesh;
	}

	glm::mat4 to_matrix(const JointTransform& transform) noexcept
	{
		const glm::mat4 translation { glm::translate(glm::mat4 { 1.0f }, transform.translation) };
		const glm::mat4 rotation { glm::mat4_cast(transform.rotation) };
		const glm::mat4 scale { glm::scale(glm::mat4 { 1.0f }, transform.scale) };

		return translation * rotation * scale;
	}
}
