#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

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

	// Copies the first size bytes of source into destination, and waits until the copy finishes
	void copy_buffer(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		const Buffer& source,
		const Buffer& destination,
		vk::DeviceSize size);
}
