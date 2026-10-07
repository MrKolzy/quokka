#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t

namespace quokka
{
	class Device;

	// Every mip level and array layer, since the engine's images have a single layer
	[[nodiscard]] constexpr vk::ImageSubresourceRange full_subresource_range(
		vk::ImageAspectFlags aspect) noexcept
	{
		return {
			.aspectMask { aspect },
			.baseMipLevel { 0 },
			// However many the image has, so the same range works with and without mipmaps
			.levelCount { vk::RemainingMipLevels },
			.baseArrayLayer { 0 },
			.layerCount { 1 }
		};
	}

	[[nodiscard]] constexpr vk::ImageSubresourceRange mip_level_subresource_range(
		vk::ImageAspectFlags aspect,
		std::uint32_t mip_level) noexcept
	{
		return {
			.aspectMask { aspect },
			.baseMipLevel { mip_level },
			.levelCount { 1 },
			.baseArrayLayer { 0 },
			.layerCount { 1 }
		};
	}

	struct Image
	{
		vk::raii::Image handle { nullptr };
		// Freed before handle is destroyed, which Vulkan allows if handle isn't used again
		vk::raii::DeviceMemory memory { nullptr };
	};

	// 2D with optimal tiling, exclusive to one queue family
	// One allocation per image, like create_buffer
	[[nodiscard]] Image create_image(
		const Device& device,
		vk::Extent2D extent,
		std::uint32_t mip_levels,
		vk::SampleCountFlagBits sample_count,
		vk::Format format,
		vk::ImageUsageFlags usage,
		vk::MemoryPropertyFlags memory_properties);

	// 2D view of the whole image
	[[nodiscard]] vk::raii::ImageView create_image_view(
		const Device& device,
		vk::Image image,
		vk::Format format,
		vk::ImageAspectFlags aspect);
}
