#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstddef> // std::byte
#include <span> // std::span

namespace quokka
{
	class Device;

	// Each buffer gets its own allocation, which is fine for a few buffers
	// but maxMemoryAllocationCount can be as low as 4096
	struct Buffer
	{
		vk::raii::Buffer handle { nullptr };
		// Freed before handle is destroyed, which Vulkan allows if handle isn't used again
		vk::raii::DeviceMemory memory { nullptr };
	};

	// Exclusive to one queue family
	[[nodiscard]] Buffer create_buffer(
		const Device& device,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags memory_properties);

	// Host visible, meant as the source of a copy into device local memory
	[[nodiscard]] Buffer create_staging_buffer(
		const Device& device,
		std::span<const std::byte> contents);

	// Blocks until the copy from a staging buffer finishes, so it's meant for loading
	// eTransferDst is added to usage for that copy
	[[nodiscard]] Buffer create_device_local_buffer(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		std::span<const std::byte> contents,
		vk::BufferUsageFlags usage);
}
