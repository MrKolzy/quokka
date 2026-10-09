#include "quokka/gltf.hpp"

#include <stdexcept> // std::runtime_error
#include <string> // std::string

namespace quokka
{
	namespace
	{
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

	AccessorData accessor_data(
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

	tinygltf::Model read_gltf(const std::filesystem::path& path)
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

		return model;
	}
}
