#include "quokka/engine.hpp"

#include "quokka/constants.hpp"

#include <array>
#include <cstdint> // std::uint#_t
#include <limits>
#include <stdexcept> // std::runtime_error

namespace quokka
{
	namespace
	{
		// Changes the layout of a color image after the source stages and accesses finish,
		// and before the destination ones start
		void transition_image_layout(
			const vk::raii::CommandBuffer& command_buffer,
			vk::Image image,
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
				// The image stays in the same queue family, so there's no ownership transfer
				.srcQueueFamilyIndex { vk::QueueFamilyIgnored },
				.dstQueueFamilyIndex { vk::QueueFamilyIgnored },
				.image { image },
				.subresourceRange { color_subresource_range }
			};

			const vk::DependencyInfo dependency_info {
				.imageMemoryBarrierCount { 1 },
				.pImageMemoryBarriers { &barrier }
			};

			command_buffer.pipelineBarrier2(dependency_info);
		}
	}

	// ***

	void Engine::run()
	{
		initialize_vulkan();
		main_loop();
	}

	void Engine::initialize_vulkan()
	{
		create_command_pool();
		create_command_buffers();
		create_frame_sync_objects();
	}

	void Engine::create_command_pool()
	{
		const vk::CommandPoolCreateInfo create_info {
			// Lets each command buffer be reset individually, since it's re-recorded every frame
			.flags { vk::CommandPoolCreateFlagBits::eResetCommandBuffer },
			// Its command buffers can only be submitted to queues of this family
			.queueFamilyIndex { device_.queue_family_index() }
		};

		command_pool_ = device_.logical_device().createCommandPool(create_info);
	}

	void Engine::create_command_buffers()
	{
		const vk::CommandBufferAllocateInfo allocate_info {
			.commandPool { *command_pool_ },
			// Primary buffers are submitted directly to a queue, secondary ones are called from primary ones
			.level { vk::CommandBufferLevel::ePrimary },
			.commandBufferCount { constants::max_frames_in_flight }
		};

		command_buffers_ = device_.logical_device().allocateCommandBuffers(allocate_info);
	}

	void Engine::create_frame_sync_objects()
	{
		// Created signaled, so the first frames don't wait forever for a submission that never happened
		constexpr vk::FenceCreateInfo fence_info {
			.flags { vk::FenceCreateFlagBits::eSignaled }
		};

		// One per frame in flight, so each frame waits only for its own previous submission
		image_available_semaphores_.reserve(constants::max_frames_in_flight);
		in_flight_fences_.reserve(constants::max_frames_in_flight);

		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			image_available_semaphores_.push_back(device_.logical_device().createSemaphore({}));
			in_flight_fences_.push_back(device_.logical_device().createFence(fence_info));
		}
	}

	void Engine::recreate_swapchain()
	{
		// A minimized window has an empty framebuffer, so wait until it's restored
		auto framebuffer_extent { window_.framebuffer_extent() };
		while (framebuffer_extent.width == 0 || framebuffer_extent.height == 0)
		{
			// The window may be closed while minimized, in which case the main loop exits without recreating anything
			if (window_.should_close())
			{
				return;
			}

			window_.wait_events();
			framebuffer_extent = window_.framebuffer_extent();
		}

		// The new swapchain queries the current size, so any resize up to this point is already handled
		window_.clear_resized();

		// Nothing may still be using the old swapchain or its image views
		device_.logical_device().waitIdle();

		swapchain_.recreate();
	}

	void Engine::main_loop()
	{
		while (!window_.should_close())
		{
			window_.poll_events();
			draw_frame();
		}

		// Drawing and presentation are asynchronous, so wait for them before destroying anything
		device_.logical_device().waitIdle();
	}

	void Engine::draw_frame()
	{
		// Wait indefinitely, the timeout is in nanoseconds
		constexpr auto no_timeout { std::numeric_limits<std::uint64_t>::max() };

		// Objects owned by the current frame in flight
		const auto& in_flight_fence { in_flight_fences_[frame_index_] };
		const auto& image_available_semaphore { image_available_semaphores_[frame_index_] };
		const auto& command_buffer { command_buffers_[frame_index_] };

		// Wait until the GPU finishes this frame's previous submission, so its command buffer can be recorded again
		if (device_.logical_device().waitForFences(*in_flight_fence, vk::True, no_timeout) != vk::Result::eSuccess)
		{
			throw std::runtime_error("Failed to wait for the in flight fence");
		}

		// The semaphore is signaled once the presentation engine stops reading the image
		// Out of date is returned instead of thrown, here and in presentKHR,
		// because the project defines VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
		const auto [acquire_result, image_index] { swapchain_.handle().acquireNextImage(no_timeout, *image_available_semaphore, nullptr) };

		// The swapchain no longer matches the surface and can't be used, so recreate it and try again next frame
		// Suboptimal is still usable, so the frame is drawn and the swapchain is recreated after presenting
		if (acquire_result == vk::Result::eErrorOutOfDateKHR)
		{
			recreate_swapchain();
			return;
		}

		// Reset only once work is sure to be submitted, otherwise an early return would leave it unsignaled forever
		device_.logical_device().resetFences(*in_flight_fence);

		// Beginning the command buffer implicitly resets it, since the pool has the reset flag
		record_command_buffer(command_buffer, image_index);

		// Indexed by image, not by frame, since it's tied to the presentation of that image
		const auto& render_finished_semaphore { swapchain_.render_finished_semaphore(image_index) };

		// Only the color writes wait for the image, the earlier stages can run before it's acquired
		const vk::SemaphoreSubmitInfo wait_semaphore_info {
			.semaphore { *image_available_semaphore },
			.stageMask { vk::PipelineStageFlagBits2::eColorAttachmentOutput }
		};

		// Signaled once every command has finished, including the final transition to the present layout
		const vk::SemaphoreSubmitInfo signal_semaphore_info {
			.semaphore { *render_finished_semaphore },
			.stageMask { vk::PipelineStageFlagBits2::eAllCommands }
		};

		const vk::CommandBufferSubmitInfo command_buffer_info {
			.commandBuffer { *command_buffer }
		};

		const vk::SubmitInfo2 submit_info {
			.waitSemaphoreInfoCount { 1 },
			.pWaitSemaphoreInfos { &wait_semaphore_info },
			.commandBufferInfoCount { 1 },
			.pCommandBufferInfos { &command_buffer_info },
			.signalSemaphoreInfoCount { 1 },
			.pSignalSemaphoreInfos { &signal_semaphore_info }
		};

		// The fence is signaled when the GPU finishes executing the command buffer
		device_.queue().submit2(submit_info, *in_flight_fence);

		const vk::PresentInfoKHR present_info {
			.waitSemaphoreCount { 1 },
			.pWaitSemaphores { &*render_finished_semaphore },
			.swapchainCount { 1 },
			.pSwapchains { &*swapchain_.handle() },
			.pImageIndices { &image_index }
		};

		const auto present_result { device_.queue().presentKHR(present_info) };

		// Recreate after presenting, so the semaphores are left in a consistent state
		// The resize flag is also checked, since not every driver reports out of date after a resize
		if (present_result == vk::Result::eErrorOutOfDateKHR ||
			present_result == vk::Result::eSuboptimalKHR ||
			window_.was_resized())
		{
			recreate_swapchain();
		}

		frame_index_ = (frame_index_ + 1) % constants::max_frames_in_flight;
	}

	void Engine::record_command_buffer(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index) const
	{
		// Re-recorded every frame, so each recording is submitted only once
		command_buffer.begin({ .flags { vk::CommandBufferUsageFlagBits::eOneTimeSubmit } });

		// Same stage the acquire semaphore waits on, so the transition happens after the image is acquired
		// The old contents are discarded, so the old layout can be undefined
		transition_image_layout(
			command_buffer,
			swapchain_.image(image_index),
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite
		);

		// Opaque black, used to clear the swapchain image before drawing
		constexpr vk::ClearValue clear_color {
			.color { .float32 { std::array { 0.0f, 0.0f, 0.0f, 1.0f } } }
		};

		// The swapchain image is cleared on load and its result kept on store so it can be presented
		const vk::RenderingAttachmentInfo color_attachment_info {
			.imageView { *swapchain_.image_view(image_index) },
			.imageLayout { vk::ImageLayout::eColorAttachmentOptimal },
			.loadOp { vk::AttachmentLoadOp::eClear },
			.storeOp { vk::AttachmentStoreOp::eStore },
			.clearValue { clear_color }
		};

		// The whole swapchain image, used as both the render area and the scissor
		const vk::Rect2D full_image_area {
			.offset { .x { 0 }, .y { 0 } },
			.extent { swapchain_.extent() }
		};

		// Without framebuffers, the attachments are given when rendering begins
		const vk::RenderingInfo rendering_info {
			.renderArea { full_image_area },
			.layerCount { 1 },
			.colorAttachmentCount { 1 },
			.pColorAttachments { &color_attachment_info }
		};

		command_buffer.beginRendering(rendering_info);

		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphics_pipeline_.handle());

		// Viewport and scissor are dynamic states, so they must be set before drawing
		const vk::Viewport viewport {
			.x { 0.0f },
			.y { 0.0f },
			.width { static_cast<float>(swapchain_.extent().width) },
			.height { static_cast<float>(swapchain_.extent().height) },
			.minDepth { 0.0f },
			.maxDepth { 1.0f }
		};

		command_buffer.setViewport(0, viewport);
		command_buffer.setScissor(0, full_image_area);

		// 3 vertices hardcoded in the shader, 1 instance, starting at the first vertex and instance
		command_buffer.draw(3, 1, 0, 0);

		command_buffer.endRendering();

		// Wait for every color write to finish before the presentation engine reads the image
		transition_image_layout(
			command_buffer,
			swapchain_.image(image_index),
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eBottomOfPipe,
			{}
		);

		command_buffer.end();
	}
}
