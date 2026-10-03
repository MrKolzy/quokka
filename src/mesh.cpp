#include "quokka/mesh.hpp"

#include <tiny_obj_loader.h>

#include <cstddef> // std::size_t
#include <functional> // std::hash
#include <initializer_list>
#include <stdexcept> // std::runtime_error
#include <string>
#include <unordered_map>

namespace quokka
{
	namespace
	{
		// Mixes the hash of value into seed, the same formula as boost::hash_combine
		void hash_combine(std::size_t& seed, float value) noexcept
		{
			seed ^= std::hash<float> {}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		}

		// Lets identical vertices be found in an unordered_map, so each one is stored only once
		struct VertexHash
		{
			[[nodiscard]] std::size_t operator()(const Vertex& vertex) const noexcept
			{
				std::size_t seed { 0 };
				for (const float component : {
					vertex.position.x, vertex.position.y, vertex.position.z,
					vertex.color.r, vertex.color.g, vertex.color.b,
					vertex.texture_coordinates.x, vertex.texture_coordinates.y })
				{
					hash_combine(seed, component);
				}

				return seed;
			}
		};
	}

	// ***

	Mesh load_obj(const std::filesystem::path& path)
	{
		// Also triangulates faces with more than three vertices
		tinyobj::ObjReader reader {};
		if (!reader.ParseFromFile(path.string()))
		{
			throw std::runtime_error("Failed to load model file: " + path.string() + "\n" + reader.Error());
		}

		// Warnings are ignored: they're usually about the .mtl material file, which isn't used
		const auto& attributes { reader.GetAttrib() };

		Mesh mesh {};
		// Index of each distinct vertex in mesh.vertices
		std::unordered_map<Vertex, std::uint32_t, VertexHash> unique_vertices {};

		for (const auto& shape : reader.GetShapes())
		{
			// One entry per corner of every triangle, each pointing to its own position and texture coordinates
			for (const auto& index : shape.mesh.indices)
			{
				if (index.texcoord_index < 0)
				{
					throw std::runtime_error("Model without texture coordinates: " + path.string());
				}

				// The attribute arrays are flat, with 3 floats per position and 2 per texture coordinate
				const auto position_index { static_cast<std::size_t>(index.vertex_index) };
				const auto texture_coordinates_index { static_cast<std::size_t>(index.texcoord_index) };

				const Vertex vertex {
					.position {
						attributes.vertices[3 * position_index + 0],
						attributes.vertices[3 * position_index + 1],
						attributes.vertices[3 * position_index + 2]
					},
					.color { 1.0f, 1.0f, 1.0f },
					// OBJ puts v = 0 at the bottom of the image, Vulkan at the top
					.texture_coordinates {
						attributes.texcoords[2 * texture_coordinates_index + 0],
						1.0f - attributes.texcoords[2 * texture_coordinates_index + 1]
					}
				};

				// Only inserted if it's new, otherwise the index of the existing one is reused
				const auto [iterator, was_inserted] { unique_vertices.try_emplace(vertex, static_cast<std::uint32_t>(mesh.vertices.size())) };
				if (was_inserted)
				{
					mesh.vertices.push_back(vertex);
				}

				mesh.indices.push_back(iterator->second);
			}
		}

		return mesh;
	}
}
