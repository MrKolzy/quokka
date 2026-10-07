#include "quokka/buffer.hpp"

#include "quokka/device.hpp"
#include "quokka/one_time_commands.hpp"

#include <cstring> // std::memcpy

namespace quokka
{
	namespace
	{
		// Copies the first size bytes of source into destination, and waits until the copy finishes
		void copy_buffer(
			const Device& device,
			const vk::raii::CommandPool& command_pool,
			const Buffer& source,
			const Buffer& destination,
			vk::DeviceSize size)
		{
			submit_one_time_commands(device, command_pool, [&source, &destination, size](const vk::raii::CommandBuffer& command_buffer)
			{
				const vk::BufferCopy copy_region {
					.srcOffset { 0 },
					.dstOffset { 0 },
					.size { size }
				};

				command_buffer.copyBuffer(*source.handle, *destination.handle, copy_region);
			});
		}
	}

	// ***

	Buffer create_buffer(
		const Device& device,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags memory_properties)
	{
		const vk::BufferCreateInfo buffer_info {
			.size { size },
			.usage { usage },
			// The engine uses a single queue, so the buffer is never shared between queue families
			.sharingMode { vk::SharingMode::eExclusive }
		};

		Buffer buffer {};
		buffer.handle = device.logical_device().createBuffer(buffer_info);

		// The size may be larger than requested, and memoryTypeBits limits which memory types can back the buffer
		const auto memory_requirements { buffer.handle.getMemoryRequirements() };

		const vk::MemoryAllocateInfo allocate_info {
			.allocationSize { memory_requirements.size },
			.memoryTypeIndex { device.find_memory_type(memory_requirements.memoryTypeBits, memory_properties) }
		};

		buffer.memory = device.logical_device().allocateMemory(allocate_info);

		// Offset 0, since the whole allocation belongs to this buffer
		buffer.handle.bindMemory(*buffer.memory, 0);

		return buffer;
	}

	Buffer create_staging_buffer(const Device& device, std::span<const std::byte> contents)
	{
		const vk::DeviceSize buffer_size { contents.size_bytes() };

		// Host visible so the CPU can map it, and host coherent so its writes reach the GPU without explicit flushes
		constexpr vk::MemoryPropertyFlags staging_memory_properties {
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		};

		auto staging_buffer { create_buffer(device, buffer_size, vk::BufferUsageFlagBits::eTransferSrc, staging_memory_properties) };

		void* const data { staging_buffer.memory.mapMemory(0, buffer_size) };
		std::memcpy(data, contents.data(), buffer_size);
		staging_buffer.memory.unmapMemory();

		return staging_buffer;
	}

	Buffer create_device_local_buffer(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		std::span<const std::byte> contents,
		vk::BufferUsageFlags usage)
	{
		const vk::DeviceSize buffer_size { contents.size_bytes() };

		// Only lives until its contents are copied into the final buffer
		const auto staging_buffer { create_staging_buffer(device, contents) };

		// Also a transfer destination, since its contents are copied from the staging buffer
		const vk::BufferUsageFlags buffer_usage { usage | vk::BufferUsageFlagBits::eTransferDst };

		// Device local memory is the fastest for the GPU to read, but on a dedicated GPU the CPU can't map it
		auto buffer { create_buffer(device, buffer_size, buffer_usage, vk::MemoryPropertyFlagBits::eDeviceLocal) };

		copy_buffer(device, command_pool, staging_buffer, buffer, buffer_size);

		return buffer;
	}
}
