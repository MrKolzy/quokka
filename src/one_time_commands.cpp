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

		// Only needed for these commands, it's freed back to the pool when it goes out of scope
		const auto command_buffer { std::move(device.logical_device().allocateCommandBuffers(allocate_info).front()) };

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

		// Every queue family with graphics support can also do transfers, so no dedicated transfer queue is needed
		device.queue().submit2(submit_info);

		// Simplest way to wait, fine for a single upload while loading; a fence would let several copies run at once
		device.queue().waitIdle();
	}
}
