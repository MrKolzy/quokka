#include "quokka/barrier.hpp"

namespace quokka
{
	void memory_barrier(
		const vk::raii::CommandBuffer& command_buffer,
		vk::PipelineStageFlags2 src_stage_mask,
		vk::AccessFlags2 src_access_mask,
		vk::PipelineStageFlags2 dst_stage_mask,
		vk::AccessFlags2 dst_access_mask) noexcept
	{
		const vk::MemoryBarrier2 barrier {
			.srcStageMask { src_stage_mask },
			.srcAccessMask { src_access_mask },
			.dstStageMask { dst_stage_mask },
			.dstAccessMask { dst_access_mask }
		};

		const vk::DependencyInfo dependency_info {
			.memoryBarrierCount { 1 },
			.pMemoryBarriers { &barrier }
		};

		command_buffer.pipelineBarrier2(dependency_info);
	}

	void transition_image_layout(
		const vk::raii::CommandBuffer& command_buffer,
		vk::Image image,
		vk::ImageSubresourceRange subresource_range,
		vk::ImageLayout old_layout,
		vk::ImageLayout new_layout,
		vk::PipelineStageFlags2 src_stage_mask,
		vk::AccessFlags2 src_access_mask,
		vk::PipelineStageFlags2 dst_stage_mask,
		vk::AccessFlags2 dst_access_mask) noexcept
	{
		const vk::ImageMemoryBarrier2 barrier {
			.srcStageMask { src_stage_mask },
			.srcAccessMask { src_access_mask },
			.dstStageMask { dst_stage_mask },
			.dstAccessMask { dst_access_mask },
			.oldLayout { old_layout },
			.newLayout { new_layout },
			// Same queue family, so there's no ownership transfer
			.srcQueueFamilyIndex { vk::QueueFamilyIgnored },
			.dstQueueFamilyIndex { vk::QueueFamilyIgnored },
			.image { image },
			.subresourceRange { subresource_range }
		};

		const vk::DependencyInfo dependency_info {
			.imageMemoryBarrierCount { 1 },
			.pImageMemoryBarriers { &barrier }
		};

		command_buffer.pipelineBarrier2(dependency_info);
	}
}
