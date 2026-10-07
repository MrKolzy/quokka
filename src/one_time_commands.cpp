#include "quokka/one_time_commands.hpp"

#include "quokka/device.hpp"

#include <utility> // std::move

namespace quokka
{
	void submit_one_time_commands(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		const std::function<void(const vk::raii::CommandBuffer&)>& record_commands)
	{
		const vk::CommandBufferAllocateInfo allocate_info {
			.commandPool { *command_pool },
			.level { vk::CommandBufferLevel::ePrimary },
			.commandBufferCount { 1 }
		};

		// Freed back to the pool when it goes out of scope
		auto command_buffers { device.logical_device().allocateCommandBuffers(allocate_info) };
		const auto command_buffer { std::move(command_buffers.front()) };

		command_buffer.begin({ .flags { vk::CommandBufferUsageFlagBits::eOneTimeSubmit } });

		record_commands(command_buffer);

		command_buffer.end();

		const vk::CommandBufferSubmitInfo command_buffer_info {
			.commandBuffer { *command_buffer }
		};

		const vk::SubmitInfo2 submit_info {
			.commandBufferInfoCount { 1 },
			.pCommandBufferInfos { &command_buffer_info }
		};

		// Graphics queues also support transfers, so no dedicated transfer queue is needed
		device.queue().submit2(submit_info);

		// Simplest way to wait, while a fence would let several uploads overlap
		device.queue().waitIdle();
	}
}
