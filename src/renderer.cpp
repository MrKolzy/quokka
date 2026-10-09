#include "quokka/renderer.hpp"

#include "quokka/camera.hpp"
#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/gpu_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/mesh.hpp"
#include "quokka/one_time_commands.hpp"
#include "quokka/swapchain.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <ktx.h>

#include <algorithm> // std::max
#include <array> // std::array
#include <bit> // std::bit_width
#include <cstdint> // std::int32_t, std::uint32_t, std::uint64_t
#include <cstring> // std::memcpy
#include <exception> // std::exception
#include <limits> // std::numeric_limits
#include <memory> // std::unique_ptr
#include <span> // std::as_bytes, std::span
#include <stdexcept> // std::runtime_error
#include <string> // std::string

namespace quokka
{
	namespace
	{
		struct CameraUniforms
		{
			// Must match CameraUniforms in shader.slang
			// Each member is a multiple of 16 bytes, the alignment uniform buffers need
			glm::mat4 view;
			glm::mat4 projection;

			// The w is unused, it's a vec4 so the size matches the shader without padding
			glm::vec4 position;
		};

		struct Transform
		{
			glm::vec3 position { 0.0f, 0.0f, 0.0f };

			// Euler angles in radians around the x, y and z axes
			glm::vec3 rotation { 0.0f, 0.0f, 0.0f };

			glm::vec3 scale { 1.0f, 1.0f, 1.0f };

			// Scales, then rotates, then moves, since the matrices apply from right to left
			[[nodiscard]] glm::mat4 model_matrix() const noexcept
			{
				glm::mat4 model { glm::translate(glm::mat4 { 1.0f }, position) };
				model = glm::rotate(model, rotation.x, glm::vec3 { 1.0f, 0.0f, 0.0f });
				model = glm::rotate(model, rotation.y, glm::vec3 { 0.0f, 1.0f, 0.0f });
				model = glm::rotate(model, rotation.z, glm::vec3 { 0.0f, 0.0f, 1.0f });

				return glm::scale(model, scale);
			}
		};

		constexpr std::uint32_t grid_object_count {
			constants::object_grid_side * constants::object_grid_side
		};

		// Between the centers of neighboring copies, a bit more than the width of the model
		constexpr float object_spacing { 2.0f };

		// A square grid on the ground centered at the origin, each copy turned differently
		[[nodiscard]] constexpr Transform object_transform(std::uint32_t object_index) noexcept
		{
			const std::uint32_t row { object_index / constants::object_grid_side };
			const std::uint32_t column { object_index % constants::object_grid_side };
			const float grid_offset {
				static_cast<float>(constants::object_grid_side - 1) * object_spacing / 2.0f
			};
			const float x { static_cast<float>(column) * object_spacing - grid_offset };
			const float z { static_cast<float>(row) * object_spacing - grid_offset };

			return {
				.position { x, 0.0f, z },
				.rotation { 0.0f, static_cast<float>(object_index) * glm::radians(40.0f), 0.0f }
			};
		}

		[[nodiscard]] constexpr vk::Rect2D full_area(vk::Extent2D extent) noexcept
		{
			return {
				.offset { .x { 0 }, .y { 0 } },
				.extent { extent }
			};
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

		// Blits each level into the next, so every level must start as a transfer destination,
		// and all of them end up ready for the fragment shader
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
				// The copy or the previous blit wrote the previous level, and this blit reads it
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

				// Never below 1, so the longer side of a non-square image keeps shrinking alone
				const std::int32_t next_mip_width { mip_width > 1 ? mip_width / 2 : 1 };
				const std::int32_t next_mip_height { mip_height > 1 ? mip_height / 2 : 1 };

				// Each region goes from the origin to the opposite corner of its whole level
				const std::array source_corners {
					vk::Offset3D {},
					vk::Offset3D { .x { mip_width }, .y { mip_height }, .z { 1 } }
				};
				const std::array destination_corners {
					vk::Offset3D {},
					vk::Offset3D { .x { next_mip_width }, .y { next_mip_height }, .z { 1 } }
				};

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

				// The previous level is final, so it can be sampled once this blit reads it
				// Waiting for a read needs no source access, only the stage
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

			// The last level is never blitted from, so it goes straight to being sampled
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

	Renderer::Renderer(
		const Device& device,
		const Swapchain& swapchain,
		const GraphicsPipeline& graphics_pipeline,
		GpuTimer& gpu_timer)
		: device_ { device }
		, swapchain_ { swapchain }
		, graphics_pipeline_ { graphics_pipeline }
		, gpu_timer_ { gpu_timer }
		, recording_threads_ { constants::recording_thread_count }
	{
		create_command_pool();
		create_command_buffers();
		create_secondary_command_buffers();

		// Only needed until it's uploaded, so the CPU copy is freed when the constructor ends
		const Mesh mesh { load_model() };
		create_vertex_buffer(mesh);
		create_index_buffer(mesh);

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

	bool Renderer::draw_frame(const Camera& camera, float elapsed_seconds)
	{
		constexpr auto no_timeout { std::numeric_limits<std::uint64_t>::max() };

		const auto& in_flight_fence { in_flight_fences_[frame_index_] };
		const auto& image_available_semaphore { image_available_semaphores_[frame_index_] };
		const auto& command_buffer { primary_command_buffers_[frame_index_] };
		const auto& descriptor_set { descriptor_sets_[frame_index_] };
		void* const mapped_uniform_buffer { mapped_uniform_buffers_[frame_index_] };

		// Waits for this frame's previous submission, so its command buffer can be reused
		if (device_.logical_device().waitForFences(*in_flight_fence, vk::True, no_timeout) !=
			vk::Result::eSuccess)
		{
			throw std::runtime_error("Failed to wait for the in flight fence");
		}

		// The fence wait above also guarantees this frame's previous timestamps are written
		gpu_timer_.collect(frame_index_);

		// The semaphore is signaled once the presentation engine stops reading the image
		// Out of date is returned instead of thrown, here and in presentKHR,
		// because the project defines VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
		const auto [acquire_result, image_index] {
			swapchain_.handle().acquireNextImage(no_timeout, *image_available_semaphore, nullptr)
		};

		// The swapchain no longer matches the surface, so recreate it and retry next frame
		// Suboptimal still works, so the frame is drawn and the swapchain is recreated afterwards
		if (acquire_result == vk::Result::eErrorOutOfDateKHR)
		{
			return true;
		}

		// Reset only once work will be submitted, or an early return would leave it unsignaled
		device_.logical_device().resetFences(*in_flight_fence);

		// The fence wait above guarantees the GPU is no longer reading this frame's uniform buffer
		update_uniform_buffer(mapped_uniform_buffer, camera);

		// Beginning the command buffer implicitly resets it, since the pool has the reset flag
		record_command_buffer(command_buffer, descriptor_set, image_index, elapsed_seconds);

		// Indexed by image, not by frame, since it's tied to the presentation of that image
		const auto& render_finished_semaphore { swapchain_.render_finished_semaphore(image_index) };

		// Only the color writes wait for the image, the earlier stages can run before it's acquired
		const vk::SemaphoreSubmitInfo wait_semaphore_info {
			.semaphore { *image_available_semaphore },
			.stageMask { vk::PipelineStageFlagBits2::eColorAttachmentOutput }
		};

		// Signaled once every command finishes, including the final transition to present
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

	Camera Renderer::initial_camera() noexcept
	{
		// Back and above the grid, far enough to see all of it, whatever its size
		constexpr float grid_size {
			static_cast<float>(constants::object_grid_side) * object_spacing
		};
		constexpr glm::vec3 camera_position {
			0.0f, grid_size * 0.7f + 1.0f, grid_size * 1.3f + 2.0f
		};

		constexpr glm::vec3 grid_center { 0.0f, 0.0f, 0.0f };

		return { camera_position, grid_center };
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
			// Submitted directly to the queue, unlike the secondary ones
			.level { vk::CommandBufferLevel::ePrimary },
			.commandBufferCount { constants::max_frames_in_flight }
		};

		primary_command_buffers_ = device_.logical_device().allocateCommandBuffers(allocate_info);
	}

	void Renderer::create_secondary_command_buffers()
	{
		const auto& logical_device { device_.logical_device() };

		// Same settings as command_pool_
		const vk::CommandPoolCreateInfo pool_info {
			.flags { vk::CommandPoolCreateFlagBits::eResetCommandBuffer },
			.queueFamilyIndex { device_.queue_family_index() }
		};

		recording_command_pools_.reserve(constants::recording_thread_count);
		secondary_command_buffers_.reserve(constants::recording_thread_count);

		for (std::uint32_t index { 0 }; index < constants::recording_thread_count; ++index)
		{
			recording_command_pools_.push_back(logical_device.createCommandPool(pool_info));

			// One per frame in flight, since the GPU may still be executing the previous one
			const vk::CommandBufferAllocateInfo allocate_info {
				.commandPool { *recording_command_pools_.back() },
				.level { vk::CommandBufferLevel::eSecondary },
				.commandBufferCount { constants::max_frames_in_flight }
			};

			secondary_command_buffers_.push_back(
				logical_device.allocateCommandBuffers(allocate_info));
		}
	}

	Mesh Renderer::load_model() const
	{
		// glTF uses y up like the world, so the vertices need no conversion
		return load_gltf(constants::model_path);
	}

	void Renderer::create_vertex_buffer(const Mesh& mesh)
	{
		vertex_buffer_ = create_device_local_buffer(
			device_,
			command_pool_,
			std::as_bytes(std::span { mesh.vertices }),
			vk::BufferUsageFlagBits::eVertexBuffer
		);
	}

	void Renderer::create_index_buffer(const Mesh& mesh)
	{
		index_buffer_ = create_device_local_buffer(
			device_,
			command_pool_,
			std::as_bytes(std::span { mesh.indices }),
			vk::BufferUsageFlagBits::eIndexBuffer
		);

		index_count_ = static_cast<std::uint32_t>(mesh.indices.size());
	}

	void Renderer::create_texture_image()
	{
		// Also inflates the texels if the file is supercompressed with zstd
		ktxTexture2* loaded_texture { nullptr };
		const auto load_result {
			ktxTexture2_CreateFromNamedFile(
				constants::texture_path,
				KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
				&loaded_texture)
		};
		if (load_result != KTX_SUCCESS)
		{
			throw std::runtime_error(
				std::string { "Failed to load texture file: " } + constants::texture_path);
		}

		// Owned by a unique_ptr, so it's destroyed even if an exception is thrown
		const std::unique_ptr<ktxTexture2, decltype(&ktxTexture2_Destroy)> texture {
			loaded_texture, &ktxTexture2_Destroy
		};

		// Block compressed formats need a GPU feature, and Basis Universal ones need transcoding
		if (texture->isCompressed || ktxTexture2_NeedsTranscoding(texture.get()))
		{
			throw std::runtime_error(
				std::string { "Compressed textures aren't supported yet: " } +
				constants::texture_path);
		}

		texture_format_ = static_cast<vk::Format>(texture->vkFormat);

		const vk::Extent2D extent {
			.width { texture->baseWidth },
			.height { texture->baseHeight }
		};

		// Mipmaps in the file are used as they are, and only generated if it has just one level
		const bool should_generate_mipmaps { texture->numLevels == 1 };

		// Halving down to 1x1 gives one level per bit needed to write the longest side
		const auto mip_levels {
			should_generate_mipmaps
				? static_cast<std::uint32_t>(std::bit_width(std::max(extent.width, extent.height)))
				: texture->numLevels
		};

		// The format comes from the file, so the GPU must be able to sample it,
		// and to filter it linearly when the blits generate the mipmaps
		const auto format_features {
			device_.physical_device().getFormatProperties(texture_format_).optimalTilingFeatures
		};
		const bool is_format_supported {
			(format_features & vk::FormatFeatureFlagBits::eSampledImage) &&
			(!should_generate_mipmaps ||
				(format_features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
		};
		if (!is_format_supported)
		{
			throw std::runtime_error(
				std::string { "The GPU can't sample the format of the texture file: " } +
				constants::texture_path);
		}

		// Every level stored in the file, one after another
		const std::span<const ktx_uint8_t> texels {
			ktxTexture_GetData(ktxTexture(texture.get())),
			ktxTexture_GetDataSize(ktxTexture(texture.get()))
		};

		// Only lives until its contents are copied into the image
		const auto staging_buffer { create_staging_buffer(device_, std::as_bytes(texels)) };

		// One copy per level stored in the file, each from that level's offset in the buffer
		std::vector<vk::BufferImageCopy> copy_regions {};
		copy_regions.reserve(texture->numLevels);

		for (std::uint32_t mip_level { 0 }; mip_level < texture->numLevels; ++mip_level)
		{
			ktx_size_t level_offset {};
			ktxTexture_GetImageOffset(ktxTexture(texture.get()), mip_level, 0, 0, &level_offset);

			const vk::BufferImageCopy copy_region {
				.bufferOffset { level_offset },
				// 0 means the texels are tightly packed, without padding between rows
				.bufferRowLength { 0 },
				.bufferImageHeight { 0 },
				.imageSubresource {
					.aspectMask { vk::ImageAspectFlagBits::eColor },
					.mipLevel { mip_level },
					.baseArrayLayer { 0 },
					.layerCount { 1 }
				},
				.imageOffset { .x { 0 }, .y { 0 }, .z { 0 } },
				// Each level halves the previous one, but never goes below 1 texel
				.imageExtent {
					.width { std::max(extent.width >> mip_level, 1u) },
					.height { std::max(extent.height >> mip_level, 1u) },
					.depth { 1 }
				}
			};

			copy_regions.push_back(copy_region);
		}

		// Written by the copy and the blits, read by the blits and then sampled by the shaders
		texture_image_ = create_image(
			device_,
			extent,
			mip_levels,
			vk::SampleCountFlagBits::e1,
			texture_format_,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
			vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		// All in one submission, so the CPU waits for the GPU only once
		submit_one_time_commands(device_, command_pool_, [&](const auto& command_buffer)
		{
			// Every level at once, with nothing to wait for since the old contents are discarded
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

			command_buffer.copyBufferToImage(
				*staging_buffer.handle,
				*texture_image_.handle,
				vk::ImageLayout::eTransferDstOptimal,
				copy_regions
			);

			if (should_generate_mipmaps)
			{
				generate_mipmaps(command_buffer, *texture_image_.handle, extent, mip_levels);
			}
			else
			{
				// Every level came from the file, so they can all be sampled once the copy is done
				transition_image_layout(
					command_buffer,
					*texture_image_.handle,
					full_subresource_range(vk::ImageAspectFlagBits::eColor),
					vk::ImageLayout::eTransferDstOptimal,
					vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::PipelineStageFlagBits2::eTransfer,
					vk::AccessFlagBits2::eTransferWrite,
					vk::PipelineStageFlagBits2::eFragmentShader,
					vk::AccessFlagBits2::eShaderRead
				);
			}
		});
	}

	void Renderer::create_texture_image_view()
	{
		texture_image_view_ = create_image_view(
			device_,
			*texture_image_.handle,
			texture_format_,
			vk::ImageAspectFlagBits::eColor
		);
	}

	void Renderer::create_texture_sampler()
	{
		const vk::SamplerCreateInfo create_info {
			// Interpolates between the nearest texels, both when magnified and minified
			.magFilter { vk::Filter::eLinear },
			.minFilter { vk::Filter::eLinear },
			// Blends the two closest mip levels, so the switch from one to the next isn't visible
			.mipmapMode { vk::SamplerMipmapMode::eLinear },
			// Coordinates outside 0 to 1 wrap around, tiling the texture
			.addressModeU { vk::SamplerAddressMode::eRepeat },
			.addressModeV { vk::SamplerAddressMode::eRepeat },
			.addressModeW { vk::SamplerAddressMode::eRepeat },
			.mipLodBias { 0.0f },
			// Keeps textures sharp at steep angles, with as many samples as the GPU allows
			.anisotropyEnable { vk::True },
			.maxAnisotropy {
				device_.physical_device().getProperties().limits.maxSamplerAnisotropy
			},
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

		// Written every frame, so the CPU writes them directly instead of using a staging buffer
		// Host visible to map them, host coherent so the writes need no explicit flushes
		constexpr vk::MemoryPropertyFlags memory_properties {
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		};

		uniform_buffers_.reserve(constants::max_frames_in_flight);
		mapped_uniform_buffers_.reserve(constants::max_frames_in_flight);

		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			uniform_buffers_.push_back(create_buffer(
				device_,
				buffer_size,
				vk::BufferUsageFlagBits::eUniformBuffer,
				memory_properties
			));

			// Never unmapped explicitly, freeing the memory unmaps it
			mapped_uniform_buffers_.push_back(
				uniform_buffers_.back().memory.mapMemory(0, buffer_size));
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
		// Parentheses because braces could take the arguments as elements
		const std::vector<vk::DescriptorSetLayout> layouts(
			constants::max_frames_in_flight,
			*graphics_pipeline_.descriptor_set_layout()
		);

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

		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			const vk::DescriptorBufferInfo buffer_info {
				.buffer { *uniform_buffers_[index].handle },
				.offset { 0 },
				.range { sizeof(CameraUniforms) }
			};

			const vk::WriteDescriptorSet camera_write {
				.dstSet { *descriptor_sets_[index] },
				// Must match camera_binding in graphics_pipeline.cpp
				.dstBinding { 0 },
				// Not an array, so the first and only element
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eUniformBuffer },
				.pBufferInfo { &buffer_info }
			};

			const vk::WriteDescriptorSet texture_write {
				.dstSet { *descriptor_sets_[index] },
				// Must match texture_binding in graphics_pipeline.cpp
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
		// Created signaled, so the first frames don't wait for a submission that never happened
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

	void Renderer::update_uniform_buffer(void* mapped_memory, const Camera& camera) const
	{
		const vk::Extent2D extent { swapchain_.extent() };
		const float aspect_ratio {
			static_cast<float>(extent.width) / static_cast<float>(extent.height)
		};

		const CameraUniforms camera_uniforms {
			.view { camera.view_matrix() },
			.projection { camera.projection_matrix(aspect_ratio) },
			.position { camera.position(), 1.0f }
		};

		std::memcpy(mapped_memory, &camera_uniforms, sizeof(camera_uniforms));
	}

	void Renderer::record_command_buffer(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		std::uint32_t image_index,
		float elapsed_seconds)
	{
		// Re-recorded every frame, so each recording is submitted only once
		command_buffer.begin({ .flags { vk::CommandBufferUsageFlagBits::eOneTimeSubmit } });

		gpu_timer_.record_timestamp(command_buffer, frame_index_, GpuTimestamp::frame_start);

		// Same stage the acquire semaphore waits on, so it happens after the image is acquired
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

		const bool is_multisampled { swapchain_.sample_count() != vk::SampleCountFlagBits::e1 };

		// Shared by every frame in flight, so wait for the previous frame before clearing it
		if (is_multisampled)
		{
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
		}

		constexpr vk::PipelineStageFlags2 depth_test_stages {
			vk::PipelineStageFlagBits2::eEarlyFragmentTests |
			vk::PipelineStageFlagBits2::eLateFragmentTests
		};
		constexpr vk::AccessFlags2 depth_accesses {
			vk::AccessFlagBits2::eDepthStencilAttachmentRead |
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite
		};

		// Shared by every frame in flight, so wait for the previous frame before clearing it
		transition_image_layout(
			command_buffer,
			swapchain_.depth_image(),
			full_subresource_range(vk::ImageAspectFlagBits::eDepth),
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthAttachmentOptimal,
			depth_test_stages,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			depth_test_stages,
			depth_accesses
		);

		constexpr vk::ClearValue clear_color {
			.color { .float32 { std::array { 0.0f, 0.0f, 0.0f, 1.0f } } }
		};

		// Drawn straight into the swapchain image, which is stored so it can be presented
		vk::RenderingAttachmentInfo color_attachment_info {
			.imageView { *swapchain_.image_view(image_index) },
			.imageLayout { vk::ImageLayout::eColorAttachmentOptimal },
			.loadOp { vk::AttachmentLoadOp::eClear },
			.storeOp { vk::AttachmentStoreOp::eStore },
			.clearValue { clear_color }
		};

		// With MSAA, drawn into the multisampled image, whose samples are averaged into this one
		// Only that result is presented, so the samples aren't stored
		if (is_multisampled)
		{
			color_attachment_info.imageView = *swapchain_.msaa_color_image_view();
			color_attachment_info.resolveMode = vk::ResolveModeFlagBits::eAverage;
			color_attachment_info.resolveImageView = *swapchain_.image_view(image_index);
			color_attachment_info.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
			color_attachment_info.storeOp = vk::AttachmentStoreOp::eDontCare;
		}

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

		// Without framebuffers, the attachments are given when rendering begins
		// The draws come from secondary command buffers, which is all this rendering can contain
		const vk::RenderingInfo rendering_info {
			.flags { vk::RenderingFlagBits::eContentsSecondaryCommandBuffers },
			.renderArea { full_area(swapchain_.extent()) },
			.layerCount { 1 },
			.colorAttachmentCount { 1 },
			.pColorAttachments { &color_attachment_info },
			.pDepthAttachment { &depth_attachment_info }
		};

		command_buffer.beginRendering(rendering_info);

		// Each thread records its share of the objects into its own secondary command buffer,
		// and run returns once all of them are done
		recording_threads_.run([this, &descriptor_set, elapsed_seconds](std::uint32_t thread_index)
		{
			// Contiguous shares, as even as possible
			const std::uint32_t first_object {
				grid_object_count * thread_index / constants::recording_thread_count
			};
			const std::uint32_t end_object {
				grid_object_count * (thread_index + 1) / constants::recording_thread_count
			};

			record_object_draws(
				secondary_command_buffers_[thread_index][frame_index_],
				descriptor_set,
				first_object,
				end_object - first_object,
				elapsed_seconds
			);
		});

		// In the order of the threads, which is also the order of the objects
		std::array<vk::CommandBuffer, constants::recording_thread_count> thread_command_buffers {};
		for (std::uint32_t index { 0 }; index < constants::recording_thread_count; ++index)
		{
			thread_command_buffers[index] = *secondary_command_buffers_[index][frame_index_];
		}

		command_buffer.executeCommands(thread_command_buffers);

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

		gpu_timer_.record_timestamp(command_buffer, frame_index_, GpuTimestamp::frame_end);

		command_buffer.end();
	}

	void Renderer::begin_secondary_command_buffer(
		const vk::raii::CommandBuffer& command_buffer) const
	{
		const vk::Format color_format { swapchain_.surface_format().format };

		// Continues the rendering of the primary command buffer, so it must match its attachments
		const vk::CommandBufferInheritanceRenderingInfo inheritance_rendering_info {
			.colorAttachmentCount { 1 },
			.pColorAttachmentFormats { &color_format },
			.depthAttachmentFormat { swapchain_.depth_format() },
			.rasterizationSamples { swapchain_.sample_count() }
		};

		const vk::CommandBufferInheritanceInfo inheritance_info {
			.pNext { &inheritance_rendering_info }
		};

		const vk::CommandBufferBeginInfo begin_info {
			// Re-recorded every frame, and only executed inside the primary one's rendering
			.flags {
				vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
				vk::CommandBufferUsageFlagBits::eRenderPassContinue
			},
			.pInheritanceInfo { &inheritance_info }
		};

		command_buffer.begin(begin_info);

		// Dynamic states, which a secondary command buffer doesn't inherit, so it sets its own
		// Negative height flips y to point up like GLM's projection, so it starts at the bottom
		const vk::Viewport viewport {
			.x { 0.0f },
			.y { static_cast<float>(swapchain_.extent().height) },
			.width { static_cast<float>(swapchain_.extent().width) },
			.height { -static_cast<float>(swapchain_.extent().height) },
			.minDepth { 0.0f },
			.maxDepth { 1.0f }
		};

		command_buffer.setViewport(0, viewport);
		command_buffer.setScissor(0, full_area(swapchain_.extent()));
	}

	void Renderer::record_object_draws(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		std::uint32_t first_object,
		std::uint32_t object_count,
		float elapsed_seconds) const
	{
		begin_secondary_command_buffer(command_buffer);

		// Secondary command buffers inherit no state, so it binds everything it uses
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphics_pipeline_.handle());

		// Starting at binding 0, with the vertex data at the start of the buffer
		command_buffer.bindVertexBuffers(0, *vertex_buffer_.handle, { 0 });

		// Offset 0, with the index type deduced from the element type of the mesh indices
		command_buffer.bindIndexBuffer(
			*index_buffer_.handle,
			0,
			vk::IndexTypeValue<decltype(Mesh::indices)::value_type>::value
		);

		// Set 0, with no dynamic offsets
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*graphics_pipeline_.pipeline_layout(),
			0,
			*descriptor_set,
			{}
		);

		const std::uint32_t end_object { first_object + object_count };

		// The objects share the mesh, texture and camera, so only the model matrix changes
		for (std::uint32_t object_index { first_object }; object_index < end_object; ++object_index)
		{
			// Each one also turns around its own vertical axis, 30 degrees per second
			Transform transform { object_transform(object_index) };
			transform.rotation.y += elapsed_seconds * glm::radians(30.0f);

			const ObjectConstants object_constants { .model { transform.model_matrix() } };
			command_buffer.pushConstants<ObjectConstants>(
				*graphics_pipeline_.pipeline_layout(),
				vk::ShaderStageFlagBits::eVertex,
				0,
				object_constants
			);

			// Every index, 1 instance, starting at the first index, vertex and instance
			command_buffer.drawIndexed(index_count_, 1, 0, 0, 0);
		}

		command_buffer.end();
	}
}
