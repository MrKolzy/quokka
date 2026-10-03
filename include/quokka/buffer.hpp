#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstddef> // std::byte
#include <span>

namespace quokka
{
	// Each buffer gets its own allocation, fine for a few buffers,
	// but drivers limit the number of allocations (maxMemoryAllocationCount can be as low as 4096)
	struct Buffer
	{
		vk::raii::Buffer handle { nullptr };
		// Backs handle, freed first since it's allocated after it
		vk::raii::DeviceMemory memory { nullptr };
	};

	// Exclusive to one queue family, backed by a new allocation of a memory type with the given properties
	[[nodiscard]] Buffer create_buffer(
		const Device& device,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags properties);

	// Host visible buffer with the given contents, the source of a copy into device local memory
	[[nodiscard]] Buffer create_staging_buffer(const Device& device, std::span<const std::byte> contents);

	// Device local buffer with the given contents, uploaded through a temporary staging buffer
	// Also usable as a transfer destination, since that's how its contents get there
	[[nodiscard]] Buffer create_device_local_buffer(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		std::span<const std::byte> contents,
		vk::BufferUsageFlags usage);
}
