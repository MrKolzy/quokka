#pragma once

#include "quokka/vertex.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint> // std::uint32_t
#include <filesystem> // std::filesystem::path
#include <string> // std::string
#include <vector> // std::vector

namespace quokka
{
	// Relative to the parent, applied as scale, then rotation, then translation, like glTF
	struct JointTransform
	{
		glm::vec3 translation { 0.0f };
		glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale { 1.0f };
	};

	// Every vector is in the order of the skin's joints, which is what Vertex::joints indexes
	struct Skeleton
	{
		// Animations made for the same rig in other files find their joints by name
		std::vector<std::string> joint_names {};

		// The parent's index, or -1 when the parent isn't a joint
		std::vector<int> parents {};

		// The pose the joints have when nothing animates them
		std::vector<JointTransform> rest_transforms {};

		// For joints whose parent isn't a joint, where the nodes above them put them
		std::vector<glm::mat4> root_transforms {};

		// From the model's space to each joint's space in the pose the mesh was modeled in
		std::vector<glm::mat4> inverse_bind_matrices {};
	};

	struct Mesh
	{
		std::vector<Vertex> vertices {};
		std::vector<std::uint32_t> indices {};
		Skeleton skeleton {};
	};

	// Merges every mesh in a .gltf or .glb file into one, which must have a single skin
	// The vertices stay in the bind pose, since skinning places them
	[[nodiscard]] Mesh load_gltf(const std::filesystem::path& path);

	// Scale, then rotation, then translation, as one matrix
	[[nodiscard]] glm::mat4 to_matrix(const JointTransform& transform) noexcept;
}
