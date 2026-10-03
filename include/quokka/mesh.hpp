#pragma once

#include "quokka/vertex.hpp"

#include <cstdint> // std::uint32_t
#include <filesystem>
#include <vector>

namespace quokka
{
	// Geometry ready to be uploaded to a vertex and an index buffer
	struct Mesh
	{
		std::vector<Vertex> vertices {};
		std::vector<std::uint32_t> indices {};
	};

	// Merges the triangles of every mesh in a .gltf or .glb file into a single mesh, with white vertex colors
	// Like in the tutorial, node transforms, materials and images are ignored, so every mesh is placed at the origin
	[[nodiscard]] Mesh load_gltf(const std::filesystem::path& path);
}
