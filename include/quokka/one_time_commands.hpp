#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <functional>

namespace quokka
{
	// Records commands with record_commands into a temporary command buffer, submits it and waits until it finishes
	// Meant for uploads while loading, not for per-frame work
	void submit_one_time_commands(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		const std::function<void(const vk::raii::CommandBuffer&)>& record_commands);
}
