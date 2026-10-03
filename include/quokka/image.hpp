#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	// Like Buffer, each image gets its own allocation
	struct Image
	{
		vk::raii::Image handle { nullptr };
		// Backs handle, freed first since it's allocated after it
		vk::raii::DeviceMemory memory { nullptr };
	};

	// 2D with optimal tiling, no mipmaps or multisampling, exclusive to one queue family and backed by a new allocation
	[[nodiscard]] Image create_image(
		const Device& device,
		vk::Extent2D extent,
		vk::Format format,
		vk::ImageUsageFlags usage,
		vk::MemoryPropertyFlags properties);
}
