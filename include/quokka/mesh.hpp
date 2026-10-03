#pragma once

#include "quokka/vertex.hpp"

#include <cstdint> // std::uint32_t
#include <filesystem>
#include <vector>

namespace quokka
{
	// Geometry ready to be uploaded to a vertex and an index buffer, with every distinct vertex stored only once
	struct Mesh
	{
		std::vector<Vertex> vertices {};
		std::vector<std::uint32_t> indices {};
	};

	// Merges every shape of the file into a single mesh, with white vertex colors since OBJ files don't have them
	[[nodiscard]] Mesh load_obj(const std::filesystem::path& path);
}
