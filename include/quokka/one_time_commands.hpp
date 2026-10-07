#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <functional> // std::function

namespace quokka
{
	class Device;

	// Blocks until the GPU finishes, so it's meant for uploads while loading, not per-frame work
	void submit_one_time_commands(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		const std::function<void(const vk::raii::CommandBuffer&)>& record_commands);
}
