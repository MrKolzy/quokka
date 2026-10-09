#include "quokka/renderer.hpp"

#include "quokka/barrier.hpp"
#include "quokka/camera.hpp"
#include "quokka/constants.hpp"
#include "quokka/damage_numbers.hpp"
#include "quokka/device.hpp"
#include "quokka/enemy_simulation.hpp"
#include "quokka/gpu_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/gui.hpp"
#include "quokka/health_bars.hpp"
#include "quokka/light.hpp"
#include "quokka/mesh.hpp"
#include "quokka/player.hpp"
#include "quokka/swapchain.hpp"

#include <glm/glm.hpp>

#include <array> // std::array
#include <cstdint> // std::uint32_t, std::uint64_t
#include <cstring> // std::memcpy
#include <exception> // std::exception
#include <limits> // std::numeric_limits
#include <stdexcept> // std::runtime_error
#include <utility> // std::move

namespace quokka
{
	namespace
	{
		struct FrameUniforms
		{
			// Must match FrameUniforms in frame.slang
			// Each vec3 is followed by a float, which fills its 16 bytes the same way as there
			glm::mat4 view;
			glm::mat4 projection;

			glm::vec3 camera_position;
			float ambient_strength;

			glm::vec3 light_direction;
			float specular_strength;

			glm::vec3 light_color;
			float shininess;

			glm::vec2 viewport_size;
			glm::vec2 aim_point;
			float time;
			float last_blast_time;
			glm::vec2 last_blast_point;
		};

		// Two matrices and five groups of 16 bytes, so nothing was padded in between
		static_assert(sizeof(FrameUniforms) == 2 * sizeof(glm::mat4) + 5 * 16);

		[[nodiscard]] constexpr vk::Rect2D full_area(vk::Extent2D extent) noexcept
		{
			return {
				.offset { .x { 0 }, .y { 0 } },
				.extent { extent }
			};
		}
	}

	// ***

	Renderer::Renderer(
		const Device& device,
		const Swapchain& swapchain,
		const GraphicsPipeline& graphics_pipeline,
		GpuTimer& gpu_timer,
		const Gui& gui,
		const DamageNumbers& damage_numbers,
		const HealthBars& health_bars,
		EnemySimulation& enemy_simulation)
		: device_ { device }
		, swapchain_ { swapchain }
		, graphics_pipeline_ { graphics_pipeline }
		, gpu_timer_ { gpu_timer }
		, gui_ { gui }
		, damage_numbers_ { damage_numbers }
		, health_bars_ { health_bars }
		, enemy_simulation_ { enemy_simulation }
		, recording_threads_ { constants::recording_thread_count }
	{
		create_command_pool();
		create_command_buffers();
		create_secondary_command_buffers();

		enemy_model_ = load_model(
			device_,
			command_pool_,
			constants::enemy_model_path,
			constants::enemy_texture_path
		);
		player_model_ = load_model(
			device_,
			command_pool_,
			constants::player_model_path,
			constants::player_texture_path
		);

		create_texture_sampler();
		create_uniform_buffers();
		create_descriptor_pool();
		create_descriptor_sets();
		enemy_material_set_ = create_material_set(enemy_model_);
		player_material_set_ = create_material_set(player_model_);
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

	bool Renderer::draw_frame(
		const Camera& camera,
		const DirectionalLight& light,
		const Player& player,
		float time_seconds)
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

		// The fence wait above also guarantees this frame's previous timestamps are written,
		// and its copy of the damage dealt to the player
		gpu_timer_.collect(frame_index_);
		enemy_simulation_.collect(frame_index_);

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
		update_uniform_buffer(mapped_uniform_buffer, camera, light, player, time_seconds);

		// Beginning the command buffer implicitly resets it, since the pool has the reset flag
		record_command_buffer(command_buffer, descriptor_set, image_index, player, time_seconds);

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

		// Executed inside the scene's rendering, like the recording threads' ones
		const vk::CommandBufferAllocateInfo main_thread_allocate_info {
			.commandPool { *command_pool_ },
			.level { vk::CommandBufferLevel::eSecondary },
			.commandBufferCount { constants::max_frames_in_flight }
		};

		main_thread_command_buffers_ =
			device_.logical_device().allocateCommandBuffers(main_thread_allocate_info);
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
		constexpr vk::DeviceSize buffer_size { sizeof(FrameUniforms) };

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
		// Per frame in flight, one uniform buffer and the enemy, damage number and combat storage
		// buffers, and per model one texture
		constexpr std::uint32_t model_count { 2 };

		constexpr std::array pool_sizes {
			vk::DescriptorPoolSize {
				.type { vk::DescriptorType::eUniformBuffer },
				.descriptorCount { constants::max_frames_in_flight }
			},
			vk::DescriptorPoolSize {
				.type { vk::DescriptorType::eStorageBuffer },
				.descriptorCount { 3 * constants::max_frames_in_flight }
			},
			vk::DescriptorPoolSize {
				.type { vk::DescriptorType::eCombinedImageSampler },
				.descriptorCount { model_count }
			}
		};

		const vk::DescriptorPoolCreateInfo create_info {
			// vk::raii::DescriptorSet frees its set when destroyed, which requires this flag
			.flags { vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet },
			.maxSets { constants::max_frames_in_flight + model_count },
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
			*graphics_pipeline_.frame_descriptor_set_layout()
		);

		const vk::DescriptorSetAllocateInfo allocate_info {
			.descriptorPool { *descriptor_pool_ },
			.descriptorSetCount { constants::max_frames_in_flight },
			.pSetLayouts { layouts.data() }
		};

		descriptor_sets_ = device_.logical_device().allocateDescriptorSets(allocate_info);

		// The simulation updates the enemies in place, and barriers order that with the draws
		const vk::DescriptorBufferInfo enemy_buffer_info {
			.buffer { *enemy_simulation_.state_buffer().handle },
			.offset { 0 },
			// The whole buffer, however many enemies it holds
			.range { vk::WholeSize }
		};

		// A single ring that every frame reads, the copies into it are ordered by barriers
		const vk::DescriptorBufferInfo damage_number_buffer_info {
			.buffer { *damage_numbers_.buffer().handle },
			.offset { 0 },
			.range { vk::WholeSize }
		};

		// Rewritten by the simulation every frame, ordered by barriers like the enemies
		const vk::DescriptorBufferInfo combat_buffer_info {
			.buffer { *enemy_simulation_.combat_buffer().handle },
			.offset { 0 },
			.range { vk::WholeSize }
		};

		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			const vk::DescriptorBufferInfo buffer_info {
				.buffer { *uniform_buffers_[index].handle },
				.offset { 0 },
				.range { sizeof(FrameUniforms) }
			};

			const vk::WriteDescriptorSet frame_write {
				.dstSet { *descriptor_sets_[index] },
				// Must match frame_binding in graphics_pipeline.cpp
				.dstBinding { 0 },
				// Not an array, so the first and only element
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eUniformBuffer },
				.pBufferInfo { &buffer_info }
			};

			const vk::WriteDescriptorSet enemy_write {
				.dstSet { *descriptor_sets_[index] },
				// Must match enemy_binding in graphics_pipeline.cpp
				.dstBinding { 1 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &enemy_buffer_info }
			};

			const vk::WriteDescriptorSet damage_number_write {
				.dstSet { *descriptor_sets_[index] },
				// Must match damage_number_binding in graphics_pipeline.cpp
				.dstBinding { 2 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &damage_number_buffer_info }
			};

			const vk::WriteDescriptorSet combat_write {
				.dstSet { *descriptor_sets_[index] },
				// Must match combat_binding in graphics_pipeline.cpp
				.dstBinding { 3 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &combat_buffer_info }
			};

			const std::array descriptor_writes {
				frame_write, enemy_write, damage_number_write, combat_write
			};

			// No copies from other descriptor sets
			device_.logical_device().updateDescriptorSets(descriptor_writes, {});
		}
	}

	vk::raii::DescriptorSet Renderer::create_material_set(const Model& model) const
	{
		const vk::DescriptorSetAllocateInfo allocate_info {
			.descriptorPool { *descriptor_pool_ },
			.descriptorSetCount { 1 },
			.pSetLayouts { &*graphics_pipeline_.material_descriptor_set_layout() }
		};

		auto material_sets { device_.logical_device().allocateDescriptorSets(allocate_info) };

		const vk::DescriptorImageInfo image_info {
			.sampler { *texture_sampler_ },
			.imageView { *model.texture_image_view },
			// The layout the texture was left in after the upload
			.imageLayout { vk::ImageLayout::eShaderReadOnlyOptimal }
		};

		const vk::WriteDescriptorSet texture_write {
			.dstSet { *material_sets[0] },
			// Must match texture_binding in graphics_pipeline.cpp
			.dstBinding { 0 },
			.dstArrayElement { 0 },
			.descriptorCount { 1 },
			.descriptorType { vk::DescriptorType::eCombinedImageSampler },
			.pImageInfo { &image_info }
		};

		// No copies from other descriptor sets
		device_.logical_device().updateDescriptorSets(texture_write, {});

		return std::move(material_sets[0]);
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

	void Renderer::update_uniform_buffer(
		void* mapped_memory,
		const Camera& camera,
		const DirectionalLight& light,
		const Player& player,
		float time_seconds) const
	{
		const vk::Extent2D extent { swapchain_.extent() };
		const glm::vec2 viewport_size {
			static_cast<float>(extent.width), static_cast<float>(extent.height)
		};

		const FrameUniforms frame_uniforms {
			.view { camera.view_matrix() },
			.projection { camera.projection_matrix(viewport_size.x / viewport_size.y) },
			.camera_position { camera.position() },
			.ambient_strength { light.ambient_strength },
			.light_direction { light.direction() },
			.specular_strength { light.specular_strength },
			.light_color { light.color },
			.shininess { light.shininess },
			.viewport_size { viewport_size },
			.aim_point { player.aim_point() },
			.time { time_seconds },
			.last_blast_time { player.last_blast_time() },
			.last_blast_point { player.last_blast_point() }
		};

		std::memcpy(mapped_memory, &frame_uniforms, sizeof(frame_uniforms));
	}

	void Renderer::record_command_buffer(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		std::uint32_t image_index,
		const Player& player,
		float time_seconds)
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

		// Before the rendering begins, since compute work and copies can't be recorded inside one
		// The hits also spawn their damage numbers here
		enemy_simulation_.record_update(command_buffer, frame_index_);

		gpu_timer_.record_timestamp(command_buffer, frame_index_, GpuTimestamp::enemies_updated);

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
		recording_threads_.run([this, &descriptor_set](std::uint32_t thread_index)
		{
			// Contiguous shares, as even as possible
			const std::uint32_t first_object {
				constants::enemy_count * thread_index / constants::recording_thread_count
			};
			const std::uint32_t end_object {
				constants::enemy_count * (thread_index + 1) / constants::recording_thread_count
			};

			record_object_draws(
				secondary_command_buffers_[thread_index][frame_index_],
				descriptor_set,
				first_object,
				end_object - first_object
			);
		});

		const auto& main_thread_command_buffer { main_thread_command_buffers_[frame_index_] };
		record_main_thread_draws(main_thread_command_buffer, descriptor_set, player);

		// In the order of the threads, which is also the order of the objects, then the main
		// thread's, so the ground comes last
		constexpr std::uint32_t scene_command_buffer_count {
			constants::recording_thread_count + 1
		};
		std::array<vk::CommandBuffer, scene_command_buffer_count> scene_command_buffers {};
		for (std::uint32_t index { 0 }; index < constants::recording_thread_count; ++index)
		{
			scene_command_buffers[index] = *secondary_command_buffers_[index][frame_index_];
		}
		scene_command_buffers.back() = *main_thread_command_buffer;

		command_buffer.executeCommands(scene_command_buffers);

		command_buffer.endRendering();

		record_overlay(command_buffer, descriptor_set, image_index, time_seconds);

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

	void Renderer::record_overlay(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		std::uint32_t image_index,
		float time_seconds) const
	{
		// Drawn over the finished scene, so it waits for its color writes, the MSAA resolve too
		// The layout stays the same, so this barrier only orders the writes
		transition_image_layout(
			command_buffer,
			swapchain_.image(image_index),
			full_subresource_range(vk::ImageAspectFlagBits::eColor),
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite
		);

		// Loaded instead of cleared, so the scene stays under the overlay
		const vk::RenderingAttachmentInfo color_attachment_info {
			.imageView { *swapchain_.image_view(image_index) },
			.imageLayout { vk::ImageLayout::eColorAttachmentOptimal },
			.loadOp { vk::AttachmentLoadOp::eLoad },
			.storeOp { vk::AttachmentStoreOp::eStore }
		};

		// One sample and no depth, since the overlay is flat and always on top
		const vk::RenderingInfo rendering_info {
			.renderArea { full_area(swapchain_.extent()) },
			.layerCount { 1 },
			.colorAttachmentCount { 1 },
			.pColorAttachments { &color_attachment_info }
		};

		command_buffer.beginRendering(rendering_info);

		// First, so the damage numbers rising from the enemies cover their bars
		health_bars_.record_draw(command_buffer, descriptor_set, swapchain_.extent());

		if (damage_numbers_.has_visible_numbers(time_seconds))
		{
			damage_numbers_.record_draw(
				command_buffer,
				descriptor_set,
				swapchain_.extent(),
				time_seconds
			);
		}

		// Last, so the interface stays on top of the damage numbers
		gui_.record(command_buffer);

		command_buffer.endRendering();
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
		std::uint32_t object_count) const
	{
		begin_secondary_command_buffer(command_buffer);

		// Secondary command buffers inherit no state, so it binds everything it uses
		command_buffer.bindPipeline(
			vk::PipelineBindPoint::eGraphics,
			*graphics_pipeline_.enemy_pipeline());
		bind_model(command_buffer, descriptor_set, enemy_model_, enemy_material_set_);

		// One draw for the whole share, where each instance reads its own enemy state
		// Every index, starting at the first index and vertex, and at the share's first instance
		command_buffer.drawIndexed(enemy_model_.index_count, object_count, 0, 0, first_object);

		command_buffer.end();
	}

	void Renderer::record_main_thread_draws(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		const Player& player) const
	{
		begin_secondary_command_buffer(command_buffer);

		command_buffer.bindPipeline(
			vk::PipelineBindPoint::eGraphics,
			*graphics_pipeline_.player_pipeline());
		bind_model(command_buffer, descriptor_set, player_model_, player_material_set_);

		// The ground also reads them, for the player's shadow
		const PlayerConstants player_constants {
			.position { player.position() },
			.yaw { player.yaw() },
			.last_hit_time { player.last_hit_time() }
		};
		command_buffer.pushConstants<PlayerConstants>(
			*graphics_pipeline_.pipeline_layout(),
			vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
			0,
			player_constants
		);

		// A single instance
		command_buffer.drawIndexed(player_model_.index_count, 1, 0, 0, 0);

		// Last, so the depth test skips the ground hidden behind the enemies and the player
		// Same pipeline layout, so the frame set and the push constants stay bound
		command_buffer.bindPipeline(
			vk::PipelineBindPoint::eGraphics,
			*graphics_pipeline_.ground_pipeline());

		// One square of two triangles, made in the vertex shader without a vertex buffer
		command_buffer.draw(6, 1, 0, 0);

		command_buffer.end();
	}

	void Renderer::bind_model(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& frame_set,
		const Model& model,
		const vk::raii::DescriptorSet& material_set) const
	{
		// Starting at binding 0, with the vertex data at the start of the buffer
		command_buffer.bindVertexBuffers(0, *model.vertex_buffer.handle, { 0 });

		// Offset 0, with the index type deduced from the element type of the mesh indices
		command_buffer.bindIndexBuffer(
			*model.index_buffer.handle,
			0,
			vk::IndexTypeValue<decltype(Mesh::indices)::value_type>::value
		);

		// Sets 0 and 1 from the first set on, with no dynamic offsets
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*graphics_pipeline_.pipeline_layout(),
			0,
			{ *frame_set, *material_set },
			{}
		);
	}
}
