#pragma once

#include "quokka/vertex.hpp"

#include <cstdint> // std::uint32_t
#include <filesystem> // std::filesystem::path
#include <vector> // std::vector

namespace quokka
{
	struct Mesh
	{
		std::vector<Vertex> vertices {};
		std::vector<std::uint32_t> indices {};
	};

	// Merges every mesh in a .gltf or .glb file into one
	// Ignores node transforms like the tutorial, so meshes stay at the origin
	[[nodiscard]] Mesh load_gltf(const std::filesystem::path& path);
}
