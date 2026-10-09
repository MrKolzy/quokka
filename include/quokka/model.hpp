#pragma once

#include "quokka/buffer.hpp"
#include "quokka/image.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <filesystem> // std::filesystem::path

namespace quokka
{
	class Device;

	struct Model
	{
		Buffer vertex_buffer {};

		// Vertices shared by several triangles are stored only once
		Buffer index_buffer {};

		// Kept from the mesh, whose CPU copy is freed once it's uploaded
		std::uint32_t index_count {};

		Image texture_image {};
		vk::raii::ImageView texture_image_view { nullptr };

		// Every clip baked for the model's skeleton, which the vertex shader skins it with
		Buffer joint_matrix_buffer {};
		Buffer clip_buffer {};
	};

	// Blocks until everything is uploaded, so it's meant for loading
	// Every mesh in the model file is drawn with the one texture in the KTX2 file
	[[nodiscard]] Model load_model(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		const std::filesystem::path& model_path,
		const std::filesystem::path& texture_path);
}
