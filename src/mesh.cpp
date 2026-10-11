#include "quokka/mesh.hpp"

#include <tiny_gltf.h>

#include <cstddef> // std::size_t
#include <cstring> // std::memcpy
#include <stdexcept> // std::runtime_error
#include <string> // std::string

namespace quokka
{
	namespace
	{
		// A negative or too big glTF index from a malformed file makes vector::at throw
		template <typename T>
		[[nodiscard]] const T& at_index(const std::vector<T>& elements, int index)
		{
			return elements.at(static_cast<std::size_t>(index));
		}

		struct AccessorData
		{
			const unsigned char* first_element;
			std::size_t stride;
			std::size_t count;
		};

		// Also checks that every element fits in the buffer
		[[nodiscard]] AccessorData accessor_data(
			const tinygltf::Model& model,
			const tinygltf::Accessor& accessor,
			std::size_t element_size)
		{
			const auto& buffer_view { at_index(model.bufferViews, accessor.bufferView) };
			const auto& buffer { at_index(model.buffers, buffer_view.buffer) };

			// The stride of the buffer view, or the size of an element when they're tightly packed
			const int byte_stride { accessor.ByteStride(buffer_view) };
			if (byte_stride <= 0 || accessor.count == 0)
			{
				throw std::runtime_error("Invalid accessor in the model file");
			}

			const auto stride { static_cast<std::size_t>(byte_stride) };
			const std::size_t first_offset { buffer_view.byteOffset + accessor.byteOffset };
			const std::size_t last_offset { first_offset + (accessor.count - 1) * stride };
			if (last_offset + element_size > buffer.data.size())
			{
				throw std::runtime_error("Accessor past the end of its buffer in the model file");
			}

			return { buffer.data.data() + first_offset, stride, accessor.count };
		}

		void append_primitive(
			const tinygltf::Model& model,
			const tinygltf::Primitive& primitive,
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

			// Indices of each primitive start at 0, so they're shifted past the earlier vertices
			const auto first_vertex { static_cast<std::uint32_t>(mesh.vertices.size()) };

			for (std::size_t index { 0 }; index < positions.count; ++index)
			{
				Vertex vertex {};
				// memcpy, since reading the bytes through a float pointer breaks C++ aliasing rules
				std::memcpy(
					&vertex.position,
					positions.first_element + index * positions.stride,
					sizeof(vertex.position));
				std::memcpy(
					&vertex.normal,
					normals.first_element + index * normals.stride,
					sizeof(vertex.normal));
				std::memcpy(
					&vertex.texture_coordinates,
					texture_coordinates.first_element + index * texture_coordinates.stride,
					sizeof(vertex.texture_coordinates));
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

		// Leaves every image undecoded, since the textures come from KTX2 files instead
		bool skip_image(
			tinygltf::Image*,
			int,
			std::string*,
			std::string*,
			int,
			int,
			const unsigned char*,
			int,
			void*) noexcept
		{
			return true;
		}
	}

	// ***

	Mesh load_gltf(const std::filesystem::path& path)
	{
		tinygltf::TinyGLTF loader {};
		loader.SetImageLoader(skip_image, nullptr);

		tinygltf::Model model {};
		std::string error {};
		// Warnings don't stop the loading, so they're ignored
		std::string warning {};

		// .glb packs the JSON and binary data in one file, .gltf is JSON that references the rest
		const bool was_loaded {
			path.extension() == ".glb"
				? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
				: loader.LoadASCIIFromFile(&model, &error, &warning, path.string())
		};
		if (!was_loaded)
		{
			throw std::runtime_error("Failed to load model file: " + path.string() + "\n" + error);
		}

		Mesh mesh {};
		for (const auto& gltf_mesh : model.meshes)
		{
			for (const auto& primitive : gltf_mesh.primitives)
			{
				append_primitive(model, primitive, mesh);
			}
		}

		return mesh;
	}
}
