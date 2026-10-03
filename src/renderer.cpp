#include "quokka/renderer.hpp"

#include "quokka/constants.hpp"
#include "quokka/one_time_commands.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale, glm::lookAt, glm::perspective
#include <stb_image.h>

#include <algorithm> // std::max
#include <array>
#include <bit> // std::bit_width
#include <chrono>
#include <cstddef> // std::size_t
#include <cstdint> // std::int32_t, std::uint#_t
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
		// Must match CameraUniforms in the shader
		// Only 4x4 matrices, which already meet the 16-byte alignment uniform buffers require
		struct CameraUniforms
		{
			glm::mat4 view;
			glm::mat4 projection;
		};

		// Position, orientation and size of an object in the world
		struct Transform
		{
			glm::vec3 position { 0.0f, 0.0f, 0.0f };
			// Euler angles in radians around the x, y and z axes
			glm::vec3 rotation { 0.0f, 0.0f, 0.0f };
			glm::vec3 scale { 1.0f, 1.0f, 1.0f };

			// Scales, then rotates and finally moves the object, since the matrices apply from right to left
			[[nodiscard]] glm::mat4 model_matrix() const noexcept
			{
				glm::mat4 model { glm::translate(glm::mat4 { 1.0f }, position) };
				model = glm::rotate(model, rotation.x, glm::vec3 { 1.0f, 0.0f, 0.0f });
				model = glm::rotate(model, rotation.y, glm::vec3 { 0.0f, 1.0f, 0.0f });
				model = glm::rotate(model, rotation.z, glm::vec3 { 0.0f, 0.0f, 1.0f });

				return glm::scale(model, scale);
			}
		};

		// Copies of the model drawn every frame: one in the center and two smaller ones behind it to the sides,
		// turned 45 degrees in opposite directions
		constexpr std::array object_transforms {
			Transform { .position { 0.0f, 0.0f, 0.0f }, .rotation { 0.0f, 0.0f, 0.0f }, .scale { 1.0f, 1.0f, 1.0f } },
			Transform { .position { -2.0f, 0.0f, -1.0f }, .rotation { 0.0f, glm::radians(45.0f), 0.0f }, .scale { 0.75f, 0.75f, 0.75f } },
			Transform { .position { 2.0f, 0.0f, -1.0f }, .rotation { 0.0f, glm::radians(-45.0f), 0.0f }, .scale { 0.75f, 0.75f, 0.75f } }
		};

		// sRGB like the texture file, so sampling converts the colors to linear
		constexpr vk::Format texture_format { vk::Format::eR8G8B8A8Srgb };

		// Changes the layout of part of an image after the source stages and accesses finish,
		// and before the destination ones start
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
				// The image stays in the same queue family, so there's no ownership transfer
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

		// Fills every mip level after the first by blitting each level into the next one at half its size
		// Expects every level in the transfer destination layout, and leaves them all ready to be sampled by the fragment shader
		void generate_mipmaps(
			const vk::raii::CommandBuffer& command_buffer,
			vk::Image image,
			vk::Extent2D extent,
			std::uint32_t mip_levels) noexcept
		{
			// Signed, like the blit offsets
			auto mip_width { static_cast<std::int32_t>(extent.width) };
			auto mip_height { static_cast<std::int32_t>(extent.height) };

			for (std::uint32_t mip_level { 1 }; mip_level < mip_levels; ++mip_level)
			{
				// The previous level was written by the copy or by the previous blit, and this blit reads it
				transition_image_layout(
					command_buffer,
					image,
					mip_level_subresource_range(vk::ImageAspectFlagBits::eColor, mip_level - 1),
					vk::ImageLayout::eTransferDstOptimal,
					vk::ImageLayout::eTransferSrcOptimal,
					vk::PipelineStageFlagBits2::eTransfer,
					vk::AccessFlagBits2::eTransferWrite,
					vk::PipelineStageFlagBits2::eTransfer,
					vk::AccessFlagBits2::eTransferRead
				);

				// Halved but never below 1 pixel, so the longer side of a non-square image keeps shrinking on its own
				const std::int32_t next_mip_width { mip_width > 1 ? mip_width / 2 : 1 };
				const std::int32_t next_mip_height { mip_height > 1 ? mip_height / 2 : 1 };

				// Each region goes from the origin to the opposite corner of its whole level
				const std::array source_corners { vk::Offset3D {}, vk::Offset3D { .x { mip_width }, .y { mip_height }, .z { 1 } } };
				const std::array destination_corners { vk::Offset3D {}, vk::Offset3D { .x { next_mip_width }, .y { next_mip_height }, .z { 1 } } };

				const vk::ImageBlit blit {
					.srcSubresource {
						.aspectMask { vk::ImageAspectFlagBits::eColor },
						.mipLevel { mip_level - 1 },
						.baseArrayLayer { 0 },
						.layerCount { 1 }
					},
					.srcOffsets { source_corners },
					.dstSubresource {
						.aspectMask { vk::ImageAspectFlagBits::eColor },
						.mipLevel { mip_level },
						.baseArrayLayer { 0 },
						.layerCount { 1 }
					},
					.dstOffsets { destination_corners }
				};

				// Linear filtering averages the texels that shrink into each one
				command_buffer.blitImage(
					image,
					vk::ImageLayout::eTransferSrcOptimal,
					image,
					vk::ImageLayout::eTransferDstOptimal,
					blit,
					vk::Filter::eLinear
				);

				// The previous level is final, so it can be sampled once this blit is done reading it
				// Only a read to wait for, so no source access is needed, just the stage
				transition_image_layout(
					command_buffer,
					image,
					mip_level_subresource_range(vk::ImageAspectFlagBits::eColor, mip_level - 1),
					vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::PipelineStageFlagBits2::eTransfer,
					{},
					vk::PipelineStageFlagBits2::eFragmentShader,
					vk::AccessFlagBits2::eShaderRead
				);

				mip_width = next_mip_width;
				mip_height = next_mip_height;
			}

			// The last level is never read by a blit, so it goes straight from being written to being sampled
			transition_image_layout(
				command_buffer,
				image,
				mip_level_subresource_range(vk::ImageAspectFlagBits::eColor, mip_levels - 1),
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite,
				vk::PipelineStageFlagBits2::eFragmentShader,
				vk::AccessFlagBits2::eShaderRead
			);
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

		// The viking room is stored with z pointing up, but the world uses y up like glTF,
		// so it's rotated -90 degrees around x once here instead of in every model matrix
		for (auto& vertex : mesh_.vertices)
		{
			vertex.position = { vertex.position.x, vertex.position.z, -vertex.position.y };
		}
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

		// Each level halves the previous one down to 1x1, so there are floor(log2(longest side)) + 1 levels,
		// which is the number of bits needed to write the longest side
		const auto mip_levels { static_cast<std::uint32_t>(std::bit_width(std::max(extent.width, extent.height))) };

		// The blits that generate the mipmaps filter linearly, which practically every GPU supports for this format,
		// but it's checked anyway in case texture_format changes
		const auto format_properties { device_.physical_device().getFormatProperties(texture_format) };
		if (!(format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
		{
			throw std::runtime_error("The texture format doesn't support the linear filtering needed to generate its mipmaps");
		}

		// One byte per channel
		constexpr std::size_t bytes_per_texel { 4 };
		const std::span<const stbi_uc> texels { pixels.get(), std::size_t { extent.width } * extent.height * bytes_per_texel };

		// Only lives until its contents are copied into the image
		const auto staging_buffer { create_staging_buffer(device_, std::as_bytes(texels)) };

		// Transfer destination of the copy and the blits, transfer source of the blits, and sampled by the shaders afterwards
		texture_image_ = create_image(
			device_,
			extent,
			mip_levels,
			vk::SampleCountFlagBits::e1,
			texture_format,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		// The transitions, the copy and the mipmap generation go in a single submission
		submit_one_time_commands(device_, command_pool_, [this, &staging_buffer, extent, mip_levels](const vk::raii::CommandBuffer& command_buffer)
		{
			// Every mip level at once: nothing to wait for, the old contents are discarded, and the copy and the blits wait for it
			transition_image_layout(
				command_buffer,
				*texture_image_.handle,
				full_subresource_range(vk::ImageAspectFlagBits::eColor),
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
					// The texels from the file go into the full size level, the others are generated from it
					.mipLevel { 0 },
					.baseArrayLayer { 0 },
					.layerCount { 1 }
				},
				.imageOffset { .x { 0 }, .y { 0 }, .z { 0 } },
				.imageExtent { .width { extent.width }, .height { extent.height }, .depth { 1 } }
			};

			command_buffer.copyBufferToImage(*staging_buffer.handle, *texture_image_.handle, vk::ImageLayout::eTransferDstOptimal, copy_region);

			// Also leaves every mip level ready to be sampled by the fragment shader
			generate_mipmaps(command_buffer, *texture_image_.handle, extent, mip_levels);
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
			// Blends the two closest mip levels, so the switch from one to the next isn't visible
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
			// Every mip level the texture has, starting at the full size one
			.minLod { 0.0f },
			.maxLod { vk::LodClampNone },
			// Only used with the clamp to border address mode
			.borderColor { vk::BorderColor::eIntOpaqueBlack },
			// Coordinates go from 0 to 1 regardless of the texture size
			.unnormalizedCoordinates { vk::False }
		};

		texture_sampler_ = device_.logical_device().createSampler(create_info);
	}

	void Renderer::create_uniform_buffers()
	{
		constexpr vk::DeviceSize buffer_size { sizeof(CameraUniforms) };

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
				.range { sizeof(CameraUniforms) }
			};

			const vk::WriteDescriptorSet camera_write {
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

			const std::array descriptor_writes { camera_write, texture_write };

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
		const float aspect_ratio { static_cast<float>(swapchain_.extent().width) / static_cast<float>(swapchain_.extent().height) };

		const CameraUniforms camera {
			// Looks at the origin from (2, 2, 6), above and to the right of the objects, with the y axis pointing up
			.view { glm::lookAt(glm::vec3 { 2.0f, 2.0f, 6.0f }, glm::vec3 { 0.0f, 0.0f, 0.0f }, glm::vec3 { 0.0f, 1.0f, 0.0f }) },
			// 45 degree vertical field of view, with the near and far planes at 0.1 and 20
			.projection { glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f, 20.0f) }
		};

		std::memcpy(mapped_memory, &camera, sizeof(camera));
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
			full_subresource_range(vk::ImageAspectFlagBits::eColor),
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite
		);

		// Shared by every frame in flight, so wait for the color writes and the resolve of the previous frame before clearing it
		// The old contents are discarded, so the old layout can be undefined
		transition_image_layout(
			command_buffer,
			swapchain_.msaa_color_image(),
			full_subresource_range(vk::ImageAspectFlagBits::eColor),
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite
		);

		// Shared by every frame in flight, so wait for the depth tests of the previous frame before clearing it
		// The old contents are discarded, so the old layout can be undefined
		transition_image_layout(
			command_buffer,
			swapchain_.depth_image(),
			full_subresource_range(vk::ImageAspectFlagBits::eDepth),
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthAttachmentOptimal,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite
		);

		// Opaque black, used to clear the multisampled image before drawing
		constexpr vk::ClearValue clear_color {
			.color { .float32 { std::array { 0.0f, 0.0f, 0.0f, 1.0f } } }
		};

		// Drawn into the multisampled image, whose samples are averaged into the swapchain image when rendering ends
		// Only that resolved result is presented, so the samples themselves aren't stored
		const vk::RenderingAttachmentInfo color_attachment_info {
			.imageView { *swapchain_.msaa_color_image_view() },
			.imageLayout { vk::ImageLayout::eColorAttachmentOptimal },
			.resolveMode { vk::ResolveModeFlagBits::eAverage },
			.resolveImageView { *swapchain_.image_view(image_index) },
			.resolveImageLayout { vk::ImageLayout::eColorAttachmentOptimal },
			.loadOp { vk::AttachmentLoadOp::eClear },
			.storeOp { vk::AttachmentStoreOp::eDontCare },
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

		// In seconds, as a float so the rotation is smooth
		const std::chrono::duration<float> elapsed_time { std::chrono::steady_clock::now() - start_time_ };

		// Every object shares the mesh, the texture and the camera, so only the pushed model matrix changes between draws
		for (const auto& transform : object_transforms)
		{
			// Each one also turns around its own vertical axis, 30 degrees per second
			Transform animated_transform { transform };
			animated_transform.rotation.y += elapsed_time.count() * glm::radians(30.0f);

			const ObjectConstants object_constants { .model { animated_transform.model_matrix() } };
			command_buffer.pushConstants<ObjectConstants>(*graphics_pipeline_.pipeline_layout(), vk::ShaderStageFlagBits::eVertex, 0, object_constants);

			// Every index in the buffer, 1 instance, starting at the first index, vertex and instance
			command_buffer.drawIndexed(static_cast<std::uint32_t>(mesh_.indices.size()), 1, 0, 0, 0);
		}

		command_buffer.endRendering();

		// Wait for every color write to finish before the presentation engine reads the image
		transition_image_layout(
			command_buffer,
			swapchain_.image(image_index),
			full_subresource_range(vk::ImageAspectFlagBits::eColor),
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
