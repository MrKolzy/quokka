#include "quokka/buffer.hpp"

#include <utility> // std::move

namespace quokka
{
	Buffer create_buffer(
		const Device& device,
		vk::DeviceSize size,
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags properties)
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
			.memoryTypeIndex { device.find_memory_type(memory_requirements.memoryTypeBits, properties) }
		};

		buffer.memory = device.logical_device().allocateMemory(allocate_info);

		// Offset 0, since the whole allocation belongs to this buffer
		buffer.handle.bindMemory(*buffer.memory, 0);

		return buffer;
	}

	void copy_buffer(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		const Buffer& source,
		const Buffer& destination,
		vk::DeviceSize size)
	{
		const vk::CommandBufferAllocateInfo allocate_info {
			.commandPool { *command_pool },
			.level { vk::CommandBufferLevel::ePrimary },
			.commandBufferCount { 1 }
		};

		// Only needed for this copy, it's freed back to the pool when it goes out of scope
		const auto command_buffer { std::move(device.logical_device().allocateCommandBuffers(allocate_info).front()) };

		command_buffer.begin({ .flags { vk::CommandBufferUsageFlagBits::eOneTimeSubmit } });

		const vk::BufferCopy copy_region {
			.srcOffset { 0 },
			.dstOffset { 0 },
			.size { size }
		};

		command_buffer.copyBuffer(*source.handle, *destination.handle, copy_region);

		command_buffer.end();

		const vk::CommandBufferSubmitInfo command_buffer_info {
			.commandBuffer { *command_buffer }
		};

		const vk::SubmitInfo2 submit_info {
			.commandBufferInfoCount { 1 },
			.pCommandBufferInfos { &command_buffer_info }
		};

		// Every queue family with graphics support can also do transfers, so no dedicated transfer queue is needed
		device.queue().submit2(submit_info);

		// Simplest way to wait, fine for a single upload while loading; a fence would let several copies run at once
		device.queue().waitIdle();
	}
}
