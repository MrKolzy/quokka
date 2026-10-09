#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	// For every resource at once, which buffers need since they have no layouts to change
	void memory_barrier(
		const vk::raii::CommandBuffer& command_buffer,
		vk::PipelineStageFlags2 src_stage_mask,
		vk::AccessFlags2 src_access_mask,
		vk::PipelineStageFlags2 dst_stage_mask,
		vk::AccessFlags2 dst_access_mask) noexcept;
}
