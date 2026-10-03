#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	// Color, no mipmapping and a single layer, the whole of a swapchain image or a texture without mipmaps
	inline constexpr vk::ImageSubresourceRange color_subresource_range {
		.aspectMask { vk::ImageAspectFlagBits::eColor },
		.baseMipLevel { 0 },
		.levelCount { 1 },
		.baseArrayLayer { 0 },
		.layerCount { 1 }
	};

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

	// 2D view of the whole color image, reading each channel as itself
	[[nodiscard]] vk::raii::ImageView create_image_view(const Device& device, vk::Image image, vk::Format format);
}
