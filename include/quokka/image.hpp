#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	// The whole of an image without mipmaps or array layers, for the given aspect (color or depth)
	[[nodiscard]] constexpr vk::ImageSubresourceRange full_subresource_range(vk::ImageAspectFlags aspect) noexcept
	{
		return {
			.aspectMask { aspect },
			.baseMipLevel { 0 },
			.levelCount { 1 },
			.baseArrayLayer { 0 },
			.layerCount { 1 }
		};
	}

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

	// 2D view of the whole image for the given aspect, reading each channel as itself
	[[nodiscard]] vk::raii::ImageView create_image_view(
		const Device& device,
		vk::Image image,
		vk::Format format,
		vk::ImageAspectFlags aspect);
}
