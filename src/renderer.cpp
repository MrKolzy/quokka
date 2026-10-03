#include "quokka/renderer.hpp"

#include "quokka/constants.hpp"
#include "quokka/one_time_commands.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // glm::rotate, glm::lookAt, glm::perspective
#include <stb_image.h>

#include <array>
#include <chrono>
#include <cstddef> // std::size_t
#include <cstdint> // std::uint#_t
#include <cstring> // std::memcpy
#include <exception>
#include <limits>
#include <memory> // std::unique_ptr
#include <span>
#include <stdexcept> // std::runtime_error
#include <string>

namespace quokka
{
	namespace
	{
		// Must match TransformUniforms in the shader
		// Only 4x4 matrices, which already meet the 16-byte alignment uniform buffers require
		struct TransformUniforms
		{
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		};

		// sRGB like the texture file, so sampling converts the colors to linear
		constexpr vk::Format texture_format { vk::Format::eR8G8B8A8Srgb };

		// Changes the layout of an image after the source stages and accesses finish,
		// and before the destination ones start
		void transition_image_layout(
			const vk::raii::CommandBuffer& command_buffer,
			vk::Image image,
			vk::ImageAspectFlags aspect,
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
				.subresourceRange { full_subresource_range(aspect) }
			};

			const vk::DependencyInfo dependency_info {
				.imageMemoryBarrierCount { 1 },
				.pImageMemoryBarriers { &barrier }
			};

			command_buffer.pipelineBarrier2(dependency_info);
		}
	}

	// ***

	Renderer::Renderer(const Device& device, const Swapchain& swapchain, const GraphicsPipeline& graphics_pipeline)
		: device_ { device }
		, swapchain_ { swapchain }
		, graphics_pipeline_ { graphics_pipeline }
	{
		create_command_pool();
		create_command_buffers();
		load_model();
		create_vertex_buffer();
		create_index_buffer();
		create_texture_image();
		create_texture_image_view();
		create_texture_sampler();
		create_uniform_buffers();
		create_descriptor_pool();
		create_descriptor_sets();
		create_frame_sync_objects();
	}

	Renderer::~Renderer()
	{
		// Drawing and presentation are asynchronous, so wait for them before destroying anything
		// Done here so it also happens when an exception leaves the main loop early
		try
		{
			device_.logical_device().waitIdle();
		}
		catch (const std::exception&)
		{
			// Destructors can't throw, and if the device is lost there's nothing left to wait for
		}
	}

	bool Renderer::draw_frame()
	{
		// Wait indefinitely, the timeout is in nanoseconds
		constexpr auto no_timeout { std::numeric_limits<std::uint64_t>::max() };

		// Objects owned by the current frame in flight
		const auto& in_flight_fence { in_flight_fences_[frame_index_] };
		const auto& image_available_semaphore { image_available_semaphores_[frame_index_] };
		const auto& command_buffer { command_buffers_[frame_index_] };
		const auto& descriptor_set { descriptor_sets_[frame_index_] };
		void* const mapped_uniform_buffer { mapped_uniform_buffers_[frame_index_] };

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
			return true;
		}

		// Reset only once work is sure to be submitted, otherwise an early return would leave it unsignaled forever
		device_.logical_device().resetFences(*in_flight_fence);

		// The fence wait above guarantees the GPU is no longer reading this frame's uniform buffer
		update_uniform_buffer(mapped_uniform_buffer);

		// Beginning the command buffer implicitly resets it, since the pool has the reset flag
		record_command_buffer(command_buffer, descriptor_set, image_index);

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

		frame_index_ = (frame_index_ + 1) % constants::max_frames_in_flight;

		// Recreate after presenting, so the semaphores are left in a consistent state
		return present_result == vk::Result::eErrorOutOfDateKHR ||
			present_result == vk::Result::eSuboptimalKHR;
	}

	void Renderer::create_command_pool()
	{
		const vk::CommandPoolCreateInfo create_info {
			// Lets each command buffer be reset individually, since it's re-recorded every frame
			.flags { vk::CommandPoolCreateFlagBits::eResetCommandBuffer },
			// Its command buffers can only be submitted to queues of this family
			.queueFamilyIndex { device_.queue_family_index() }
		};

		command_pool_ = device_.logical_device().createCommandPool(create_info);
	}

	void Renderer::create_command_buffers()
	{
		const vk::CommandBufferAllocateInfo allocate_info {
			.commandPool { *command_pool_ },
			// Primary buffers are submitted directly to a queue, secondary ones are called from primary ones
			.level { vk::CommandBufferLevel::ePrimary },
			.commandBufferCount { constants::max_frames_in_flight }
		};

		command_buffers_ = device_.logical_device().allocateCommandBuffers(allocate_info);
	}

	void Renderer::load_model()
	{
		mesh_ = load_obj(constants::model_path);
	}

	void Renderer::create_vertex_buffer()
	{
		vertex_buffer_ = create_device_local_buffer(
			device_,
			command_pool_,
			std::as_bytes(std::span { mesh_.vertices }),
			vk::BufferUsageFlagBits::eVertexBuffer
		);
	}

	void Renderer::create_index_buffer()
	{
		index_buffer_ = create_device_local_buffer(
			device_,
			command_pool_,
			std::as_bytes(std::span { mesh_.indices }),
			vk::BufferUsageFlagBits::eIndexBuffer
		);
	}

	void Renderer::create_texture_image()
	{
		int width {};
		int height {};
		int channels {};

		// Always 4 channels (RGBA), even if the file has fewer, so every texel matches the image format
		// Owned by a unique_ptr, so stbi_image_free also runs if an exception is thrown before the end
		const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels {
			stbi_load(constants::texture_path, &width, &height, &channels, STBI_rgb_alpha),
			&stbi_image_free
		};

		if (!pixels)
		{
			throw std::runtime_error(std::string { "Failed to load texture file: " } + constants::texture_path);
		}

		const vk::Extent2D extent {
			.width { static_cast<std::uint32_t>(width) },
			.height { static_cast<std::uint32_t>(height) }
		};

		// One byte per channel
		constexpr std::size_t bytes_per_texel { 4 };
		const std::span<const stbi_uc> texels { pixels.get(), std::size_t { extent.width } * extent.height * bytes_per_texel };

		// Only lives until its contents are copied into the image
		const auto staging_buffer { create_staging_buffer(device_, std::as_bytes(texels)) };

		// A transfer destination for the copy, and sampled by the shaders afterwards
		texture_image_ = create_image(
			device_,
			extent,
			texture_format,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		// Both transitions and the copy go in a single submission
		submit_one_time_commands(device_, command_pool_, [this, &staging_buffer, extent](const vk::raii::CommandBuffer& command_buffer)
		{
			// Nothing to wait for, the old contents are discarded, and the copy waits for the transition
			transition_image_layout(
				command_buffer,
				*texture_image_.handle,
				vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal,
				vk::PipelineStageFlagBits2::eNone,
				{},
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite
			);

			const vk::BufferImageCopy copy_region {
				.bufferOffset { 0 },
				// 0 means the texels are tightly packed, without padding between rows
				.bufferRowLength { 0 },
				.bufferImageHeight { 0 },
				.imageSubresource {
					.aspectMask { vk::ImageAspectFlagBits::eColor },
					.mipLevel { 0 },
					.baseArrayLayer { 0 },
					.layerCount { 1 }
				},
				.imageOffset { .x { 0 }, .y { 0 }, .z { 0 } },
				.imageExtent { .width { extent.width }, .height { extent.height }, .depth { 1 } }
			};

			command_buffer.copyBufferToImage(*staging_buffer.handle, *texture_image_.handle, vk::ImageLayout::eTransferDstOptimal, copy_region);

			// Sampling in the fragment shader waits for the copy to finish
			transition_image_layout(
				command_buffer,
				*texture_image_.handle,
				vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite,
				vk::PipelineStageFlagBits2::eFragmentShader,
				vk::AccessFlagBits2::eShaderRead
			);
		});
	}

	void Renderer::create_texture_image_view()
	{
		texture_image_view_ = create_image_view(device_, *texture_image_.handle, texture_format, vk::ImageAspectFlagBits::eColor);
	}

	void Renderer::create_texture_sampler()
	{
		const vk::SamplerCreateInfo create_info {
			// Interpolate between the nearest texels instead of picking one, both when magnified and minified
			.magFilter { vk::Filter::eLinear },
			.minFilter { vk::Filter::eLinear },
			// Only matters with mipmaps, which the texture doesn't have yet
			.mipmapMode { vk::SamplerMipmapMode::eLinear },
			// Coordinates outside 0 to 1 wrap around, tiling the texture
			.addressModeU { vk::SamplerAddressMode::eRepeat },
			.addressModeV { vk::SamplerAddressMode::eRepeat },
			.addressModeW { vk::SamplerAddressMode::eRepeat },
			.mipLodBias { 0.0f },
			// Keeps textures sharp when seen at steep angles, with as many samples as the GPU allows
			.anisotropyEnable { vk::True },
			.maxAnisotropy { device_.physical_device().getProperties().limits.maxSamplerAnisotropy },
			// Comparing texels against a reference value is for shadow maps
			.compareEnable { vk::False },
			.compareOp { vk::CompareOp::eAlways },
			// A single mip level
			.minLod { 0.0f },
			.maxLod { 0.0f },
			// Only used with the clamp to border address mode
			.borderColor { vk::BorderColor::eIntOpaqueBlack },
			// Coordinates go from 0 to 1 regardless of the texture size
			.unnormalizedCoordinates { vk::False }
		};

		texture_sampler_ = device_.logical_device().createSampler(create_info);
	}

	void Renderer::create_uniform_buffers()
	{
		constexpr vk::DeviceSize buffer_size { sizeof(TransformUniforms) };

		// Written every frame, so the CPU writes them directly instead of going through a staging buffer
		// Host visible so the CPU can map them, and host coherent so its writes reach the GPU without explicit flushes
		constexpr vk::MemoryPropertyFlags memory_properties {
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		};

		uniform_buffers_.reserve(constants::max_frames_in_flight);
		mapped_uniform_buffers_.reserve(constants::max_frames_in_flight);

		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			uniform_buffers_.push_back(create_buffer(device_, buffer_size, vk::BufferUsageFlagBits::eUniformBuffer, memory_properties));

			// Never unmapped explicitly, freeing the memory unmaps it
			mapped_uniform_buffers_.push_back(uniform_buffers_.back().memory.mapMemory(0, buffer_size));
		}
	}

	void Renderer::create_descriptor_pool()
	{
		// One uniform buffer and one texture descriptor per frame in flight
		constexpr std::array pool_sizes {
			vk::DescriptorPoolSize {
				.type { vk::DescriptorType::eUniformBuffer },
				.descriptorCount { constants::max_frames_in_flight }
			},
			vk::DescriptorPoolSize {
				.type { vk::DescriptorType::eCombinedImageSampler },
				.descriptorCount { constants::max_frames_in_flight }
			}
		};

		const vk::DescriptorPoolCreateInfo create_info {
			// vk::raii::DescriptorSet frees its set when destroyed, which requires this flag
			.flags { vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet },
			.maxSets { constants::max_frames_in_flight },
			.poolSizeCount { static_cast<std::uint32_t>(pool_sizes.size()) },
			.pPoolSizes { pool_sizes.data() }
		};

		descriptor_pool_ = device_.logical_device().createDescriptorPool(create_info);
	}

	void Renderer::create_descriptor_sets()
	{
		// Every set has the same layout, but the allocation takes one per set
		// Parentheses, not braces: this is the count constructor
		const std::vector<vk::DescriptorSetLayout> layouts(constants::max_frames_in_flight, *graphics_pipeline_.descriptor_set_layout());

		const vk::DescriptorSetAllocateInfo allocate_info {
			.descriptorPool { *descriptor_pool_ },
			.descriptorSetCount { constants::max_frames_in_flight },
			.pSetLayouts { layouts.data() }
		};

		descriptor_sets_ = device_.logical_device().allocateDescriptorSets(allocate_info);

		// The same for every frame, since the texture is never written after loading
		const vk::DescriptorImageInfo image_info {
			.sampler { *texture_sampler_ },
			.imageView { *texture_image_view_ },
			// The layout the texture was left in after the upload
			.imageLayout { vk::ImageLayout::eShaderReadOnlyOptimal }
		};

		// Point each set at the uniform buffer of its frame and at the texture
		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			const vk::DescriptorBufferInfo buffer_info {
				.buffer { *uniform_buffers_[index].handle },
				.offset { 0 },
				.range { sizeof(TransformUniforms) }
			};

			const vk::WriteDescriptorSet transforms_write {
				.dstSet { *descriptor_sets_[index] },
				.dstBinding { 0 },
				// Not an array, so the first and only element
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eUniformBuffer },
				.pBufferInfo { &buffer_info }
			};

			const vk::WriteDescriptorSet texture_write {
				.dstSet { *descriptor_sets_[index] },
				.dstBinding { 1 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eCombinedImageSampler },
				.pImageInfo { &image_info }
			};

			const std::array descriptor_writes { transforms_write, texture_write };

			// No copies from other descriptor sets
			device_.logical_device().updateDescriptorSets(descriptor_writes, {});
		}
	}

	void Renderer::create_frame_sync_objects()
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

	void Renderer::update_uniform_buffer(void* mapped_memory) const
	{
		// In seconds, as a float so the rotation is smooth
		const std::chrono::duration<float> elapsed_time { std::chrono::steady_clock::now() - start_time_ };

		const float aspect_ratio { static_cast<float>(swapchain_.extent().width) / static_cast<float>(swapchain_.extent().height) };

		const TransformUniforms transforms {
			// Rotates 90 degrees per second around the z axis
			.model { glm::rotate(glm::mat4 { 1.0f }, elapsed_time.count() * glm::radians(90.0f), glm::vec3 { 0.0f, 0.0f, 1.0f }) },
			// Looks at the origin diagonally from (2, 2, 2), with the z axis pointing up
			.view { glm::lookAt(glm::vec3 { 2.0f, 2.0f, 2.0f }, glm::vec3 { 0.0f, 0.0f, 0.0f }, glm::vec3 { 0.0f, 0.0f, 1.0f }) },
			// 45 degree vertical field of view, with the near and far planes at 0.1 and 10
			.projection { glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f, 10.0f) }
		};

		std::memcpy(mapped_memory, &transforms, sizeof(transforms));
	}

	void Renderer::record_command_buffer(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		std::uint32_t image_index) const
	{
		// Re-recorded every frame, so each recording is submitted only once
		command_buffer.begin({ .flags { vk::CommandBufferUsageFlagBits::eOneTimeSubmit } });

		// Same stage the acquire semaphore waits on, so the transition happens after the image is acquired
		// The old contents are discarded, so the old layout can be undefined
		transition_image_layout(
			command_buffer,
			swapchain_.image(image_index),
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite
		);

		// Shared by every frame in flight, so wait for the depth tests of the previous frame before clearing it
		// The old contents are discarded, so the old layout can be undefined
		transition_image_layout(
			command_buffer,
			swapchain_.depth_image(),
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthAttachmentOptimal,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite
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

		// The far plane, so anything drawn is closer
		constexpr vk::ClearValue clear_depth {
			.depthStencil { .depth { 1.0f }, .stencil { 0 } }
		};

		// Cleared on load, and not stored since it's only needed while drawing this frame
		const vk::RenderingAttachmentInfo depth_attachment_info {
			.imageView { *swapchain_.depth_image_view() },
			.imageLayout { vk::ImageLayout::eDepthAttachmentOptimal },
			.loadOp { vk::AttachmentLoadOp::eClear },
			.storeOp { vk::AttachmentStoreOp::eDontCare },
			.clearValue { clear_depth }
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
			.pColorAttachments { &color_attachment_info },
			.pDepthAttachment { &depth_attachment_info }
		};

		command_buffer.beginRendering(rendering_info);

		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphics_pipeline_.handle());

		// Starting at binding 0, with the vertex data at the start of the buffer
		command_buffer.bindVertexBuffers(0, *vertex_buffer_.handle, { 0 });

		// Offset 0, with the index type deduced from the element type of the mesh indices
		command_buffer.bindIndexBuffer(*index_buffer_.handle, 0, vk::IndexTypeValue<decltype(mesh_.indices)::value_type>::value);

		// Set 0, with no dynamic offsets
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*graphics_pipeline_.pipeline_layout(),
			0,
			*descriptor_set,
			{}
		);

		// Viewport and scissor are dynamic states, so they must be set before drawing
		// Negative height flips the y axis to point up like in GLM's projection, so it starts at the bottom edge
		const vk::Viewport viewport {
			.x { 0.0f },
			.y { static_cast<float>(swapchain_.extent().height) },
			.width { static_cast<float>(swapchain_.extent().width) },
			.height { -static_cast<float>(swapchain_.extent().height) },
			.minDepth { 0.0f },
			.maxDepth { 1.0f }
		};

		command_buffer.setViewport(0, viewport);
		command_buffer.setScissor(0, full_image_area);

		// Every index in the buffer, 1 instance, starting at the first index, vertex and instance
		command_buffer.drawIndexed(static_cast<std::uint32_t>(mesh_.indices.size()), 1, 0, 0, 0);

		command_buffer.endRendering();

		// Wait for every color write to finish before the presentation engine reads the image
		transition_image_layout(
			command_buffer,
			swapchain_.image(image_index),
			vk::ImageAspectFlagBits::eColor,
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
