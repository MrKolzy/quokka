#include "quokka/particle_system.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/shader_module.hpp"

#include <glm/glm.hpp>

#include <algorithm> // std::min
#include <array>
#include <cmath> // std::cos, std::sin, std::sqrt
#include <cstddef> // offsetof
#include <cstdint> // std::uint32_t
#include <numbers> // std::numbers::pi_v
#include <random>
#include <span>
#include <vector>

namespace quokka
{
	namespace
	{
		// Must match Particle in the shader, all vec4 so both sides agree on the layout, with w unused in the position and velocity
		struct Particle
		{
			glm::vec4 position;
			glm::vec4 velocity;
			glm::vec4 color;
		};

		// Must match UpdateConstants in the shader, pushed before each dispatch
		struct UpdateConstants
		{
			glm::vec4 box_min;
			glm::vec4 box_max;
			float delta_time;
			std::uint32_t particle_count;
		};

		// Invocations per work group, must match numthreads in the shader
		constexpr std::uint32_t work_group_size { 256 };

		// The box the particles fly in, centered on the grid of objects and about as wide as it
		constexpr glm::vec3 box_min { -10.0f, 0.0f, -10.0f };
		constexpr glm::vec3 box_max { 10.0f, 6.0f, 10.0f };

		// The longest time a single update simulates, so the particles don't jump after a long frame, like while the window is dragged
		constexpr float max_delta_seconds { 0.1f };

		// Random positions inside the box, moving in random directions with warm colors, like sparks
		// From a fixed seed, so every run starts the same and measurements can be repeated
		[[nodiscard]] std::vector<Particle> create_initial_particles()
		{
			std::mt19937 random_engine { 2026 };
			std::uniform_real_distribution<float> random_unit { 0.0f, 1.0f };

			std::vector<Particle> particles {};
			particles.reserve(constants::particle_count);

			for (std::uint32_t index { 0 }; index < constants::particle_count; ++index)
			{
				const glm::vec3 random_point { random_unit(random_engine), random_unit(random_engine), random_unit(random_engine) };
				const glm::vec3 position { glm::mix(box_min, box_max, random_point) };

				// Uniformly distributed over a sphere, from a random height and a random angle around the vertical axis
				const float height { 2.0f * random_unit(random_engine) - 1.0f };
				const float angle { 2.0f * std::numbers::pi_v<float> * random_unit(random_engine) };
				const float radius { std::sqrt(1.0f - height * height) };
				const glm::vec3 direction { radius * std::cos(angle), height, radius * std::sin(angle) };

				// Between 1 and 3 units per second
				const float speed { 1.0f + 2.0f * random_unit(random_engine) };

				// From orange to yellow, fully opaque
				const glm::vec4 color { 1.0f, 0.4f + 0.5f * random_unit(random_engine), 0.1f, 1.0f };

				particles.push_back({
					.position { position, 0.0f },
					.velocity { direction * speed, 0.0f },
					.color { color }
				});
			}

			return particles;
		}

		// The destination accesses of later commands wait for the source accesses of earlier ones, whose results they then see
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
	}

	// ***

	ParticleSystem::ParticleSystem(
		const Device& device,
		const GraphicsPipeline& graphics_pipeline,
		vk::Format color_format,
		vk::Format depth_format,
		vk::SampleCountFlagBits sample_count)
		: device_ { device }
	{
		create_storage_buffers();
		create_update_descriptor_set_layout();
		create_descriptor_pool();
		create_update_descriptor_sets();

		// The update and both drawing entry points live in the same compiled module
		const auto shader_module { create_shader_module(device_.logical_device(), read_shader_file(constants::particle_shader_path)) };

		create_update_pipeline(shader_module);
		create_draw_pipeline(shader_module, graphics_pipeline.descriptor_set_layout(), color_format, depth_format, sample_count);
	}

	void ParticleSystem::record_update(
		const vk::raii::CommandBuffer& command_buffer,
		std::uint32_t frame_index,
		float delta_seconds) const
	{
		// The update reads what the previous update wrote, and overwrites what an earlier one wrote and read
		// The draw that read this frame's buffer as vertices is already done, since this frame's fence was waited on
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
		);

		command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *update_pipeline_);

		// Set 0, with no dynamic offsets
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eCompute,
			*update_pipeline_layout_,
			0,
			*update_descriptor_sets_[frame_index],
			{}
		);

		const UpdateConstants update_constants {
			.box_min { box_min, 0.0f },
			.box_max { box_max, 0.0f },
			.delta_time { std::min(delta_seconds, max_delta_seconds) },
			.particle_count { constants::particle_count }
		};

		command_buffer.pushConstants<UpdateConstants>(*update_pipeline_layout_, vk::ShaderStageFlagBits::eCompute, 0, update_constants);

		// Enough work groups to cover every particle, rounding up
		command_buffer.dispatch((constants::particle_count + work_group_size - 1) / work_group_size, 1, 1);

		// The draw reads as vertices what the update just wrote
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eVertexAttributeInput,
			vk::AccessFlagBits2::eVertexAttributeRead
		);
	}

	void ParticleSystem::record_draw(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& camera_descriptor_set,
		std::uint32_t frame_index) const
	{
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *draw_pipeline_);

		// The buffer the update of this frame wrote, starting at binding 0 with the particles at the start of the buffer
		command_buffer.bindVertexBuffers(0, *storage_buffers_[frame_index].handle, { 0 });

		// Set 0, with no dynamic offsets
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*draw_pipeline_layout_,
			0,
			*camera_descriptor_set,
			{}
		);

		// One point per particle, 1 instance, starting at the first vertex and instance
		command_buffer.draw(constants::particle_count, 1, 0, 0);
	}

	void ParticleSystem::create_storage_buffers()
	{
		const auto particles { create_initial_particles() };

		// Only needed for the uploads, so it's destroyed when this function returns
		const vk::CommandPoolCreateInfo pool_info {
			// Its command buffers are short lived, each one used for a single copy
			.flags { vk::CommandPoolCreateFlagBits::eTransient },
			// Its command buffers can only be submitted to queues of this family
			.queueFamilyIndex { device_.queue_family_index() }
		};

		const auto upload_command_pool { device_.logical_device().createCommandPool(pool_info) };

		// Written by the compute shader, and read as vertices when drawing
		constexpr vk::BufferUsageFlags usage { vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer };

		storage_buffers_.reserve(constants::max_frames_in_flight);

		// Every frame in flight starts with the same particles, so the first update can read any of them as the previous frame
		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			storage_buffers_.push_back(create_device_local_buffer(device_, upload_command_pool, std::as_bytes(std::span { particles }), usage));
		}
	}

	void ParticleSystem::create_update_descriptor_set_layout()
	{
		// The particles of the previous frame, only read by the compute shader
		constexpr vk::DescriptorSetLayoutBinding previous_particles_binding {
			.binding { 1 },
			.descriptorType { vk::DescriptorType::eStorageBuffer },
			// A single storage buffer, not an array of them
			.descriptorCount { 1 },
			.stageFlags { vk::ShaderStageFlagBits::eCompute }
		};

		// The particles of the current frame, written by the compute shader
		constexpr vk::DescriptorSetLayoutBinding current_particles_binding {
			.binding { 2 },
			.descriptorType { vk::DescriptorType::eStorageBuffer },
			.descriptorCount { 1 },
			.stageFlags { vk::ShaderStageFlagBits::eCompute }
		};

		// Bindings 1 and 2, since binding 0 of the shader file is the camera used when drawing
		constexpr std::array bindings { previous_particles_binding, current_particles_binding };

		const vk::DescriptorSetLayoutCreateInfo create_info {
			.bindingCount { static_cast<std::uint32_t>(bindings.size()) },
			.pBindings { bindings.data() }
		};

		update_descriptor_set_layout_ = device_.logical_device().createDescriptorSetLayout(create_info);
	}

	void ParticleSystem::create_descriptor_pool()
	{
		// Two storage buffer descriptors per frame in flight, the previous and the current particles
		constexpr vk::DescriptorPoolSize pool_size {
			.type { vk::DescriptorType::eStorageBuffer },
			.descriptorCount { 2 * constants::max_frames_in_flight }
		};

		const vk::DescriptorPoolCreateInfo create_info {
			// vk::raii::DescriptorSet frees its set when destroyed, which requires this flag
			.flags { vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet },
			.maxSets { constants::max_frames_in_flight },
			.poolSizeCount { 1 },
			.pPoolSizes { &pool_size }
		};

		descriptor_pool_ = device_.logical_device().createDescriptorPool(create_info);
	}

	void ParticleSystem::create_update_descriptor_sets()
	{
		// Every set has the same layout, but the allocation takes one per set
		// Parentheses, not braces: this is the count constructor
		const std::vector<vk::DescriptorSetLayout> layouts(constants::max_frames_in_flight, *update_descriptor_set_layout_);

		const vk::DescriptorSetAllocateInfo allocate_info {
			.descriptorPool { *descriptor_pool_ },
			.descriptorSetCount { constants::max_frames_in_flight },
			.pSetLayouts { layouts.data() }
		};

		update_descriptor_sets_ = device_.logical_device().allocateDescriptorSets(allocate_info);

		constexpr vk::DeviceSize buffer_size { sizeof(Particle) * constants::particle_count };

		// Point each set at the storage buffer of the previous frame to read and at the one of its own frame to write
		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			// Wraps around, so the previous frame of the first one is the last
			const std::uint32_t previous_index { (index + constants::max_frames_in_flight - 1) % constants::max_frames_in_flight };

			const vk::DescriptorBufferInfo previous_buffer_info {
				.buffer { *storage_buffers_[previous_index].handle },
				.offset { 0 },
				.range { buffer_size }
			};

			const vk::DescriptorBufferInfo current_buffer_info {
				.buffer { *storage_buffers_[index].handle },
				.offset { 0 },
				.range { buffer_size }
			};

			const vk::WriteDescriptorSet previous_particles_write {
				.dstSet { *update_descriptor_sets_[index] },
				.dstBinding { 1 },
				// Not an array, so the first and only element
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &previous_buffer_info }
			};

			const vk::WriteDescriptorSet current_particles_write {
				.dstSet { *update_descriptor_sets_[index] },
				.dstBinding { 2 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &current_buffer_info }
			};

			const std::array descriptor_writes { previous_particles_write, current_particles_write };

			// No copies from other descriptor sets
			device_.logical_device().updateDescriptorSets(descriptor_writes, {});
		}
	}

	void ParticleSystem::create_update_pipeline(const vk::raii::ShaderModule& shader_module)
	{
		// The box, the time step and the particle count, read only by the compute shader
		constexpr vk::PushConstantRange update_constants_range {
			.stageFlags { vk::ShaderStageFlagBits::eCompute },
			.offset { 0 },
			.size { sizeof(UpdateConstants) }
		};

		// A single descriptor set, set 0 in the shader
		const vk::PipelineLayoutCreateInfo pipeline_layout_info {
			.setLayoutCount { 1 },
			.pSetLayouts { &*update_descriptor_set_layout_ },
			.pushConstantRangeCount { 1 },
			.pPushConstantRanges { &update_constants_range }
		};

		update_pipeline_layout_ = device_.logical_device().createPipelineLayout(pipeline_layout_info);

		// Must match the entry point name in the shader and in the slangc command
		const vk::PipelineShaderStageCreateInfo compute_shader_stage_info {
			.stage { vk::ShaderStageFlagBits::eCompute },
			.module { *shader_module },
			.pName { "update_main" }
		};

		const vk::ComputePipelineCreateInfo pipeline_info {
			.stage { compute_shader_stage_info },
			.layout { *update_pipeline_layout_ }
		};

		// No pipeline cache, like the graphics pipelines
		update_pipeline_ = device_.logical_device().createComputePipeline(nullptr, pipeline_info);
	}

	void ParticleSystem::create_draw_pipeline(
		const vk::raii::ShaderModule& shader_module,
		const vk::raii::DescriptorSetLayout& camera_descriptor_set_layout,
		vk::Format color_format,
		vk::Format depth_format,
		vk::SampleCountFlagBits sample_count)
	{
		// The same set 0 as the objects, with the camera, but no push constants
		const vk::PipelineLayoutCreateInfo pipeline_layout_info {
			.setLayoutCount { 1 },
			.pSetLayouts { &*camera_descriptor_set_layout }
		};

		draw_pipeline_layout_ = device_.logical_device().createPipelineLayout(pipeline_layout_info);

		// Must match the entry point names in the shader and in the slangc command
		const vk::PipelineShaderStageCreateInfo vertex_shader_stage_info {
			.stage { vk::ShaderStageFlagBits::eVertex },
			.module { *shader_module },
			.pName { "particle_vert_main" }
		};

		const vk::PipelineShaderStageCreateInfo fragment_shader_stage_info {
			.stage { vk::ShaderStageFlagBits::eFragment },
			.module { *shader_module },
			.pName { "particle_frag_main" }
		};

		const std::array shader_stages { vertex_shader_stage_info, fragment_shader_stage_info };

		// The storage buffers are read as vertex buffers, one particle per vertex
		constexpr vk::VertexInputBindingDescription binding_description {
			.binding { 0 },
			.stride { sizeof(Particle) },
			.inputRate { vk::VertexInputRate::eVertex }
		};

		// float3 and float4 in the shader, skipping the velocity, which only the update needs
		constexpr std::array attribute_descriptions {
			vk::VertexInputAttributeDescription {
				.location { 0 },
				.binding { 0 },
				.format { vk::Format::eR32G32B32Sfloat },
				.offset { offsetof(Particle, position) }
			},
			vk::VertexInputAttributeDescription {
				.location { 1 },
				.binding { 0 },
				.format { vk::Format::eR32G32B32A32Sfloat },
				.offset { offsetof(Particle, color) }
			}
		};

		const vk::PipelineVertexInputStateCreateInfo vertex_input_info {
			.vertexBindingDescriptionCount { 1 },
			.pVertexBindingDescriptions { &binding_description },
			.vertexAttributeDescriptionCount { static_cast<std::uint32_t>(attribute_descriptions.size()) },
			.pVertexAttributeDescriptions { attribute_descriptions.data() }
		};

		// Every vertex is a point of its own, with the size the vertex shader gives it
		constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_info {
			.topology { vk::PrimitiveTopology::ePointList }
		};

		// Only the counts, the actual viewport and scissor are dynamic
		constexpr vk::PipelineViewportStateCreateInfo viewport_state_info {
			.viewportCount { 1 },
			.scissorCount { 1 }
		};

		// Like the objects, except that points have no back faces to discard
		constexpr vk::PipelineRasterizationStateCreateInfo rasterization_info {
			.depthClampEnable { vk::False },
			.rasterizerDiscardEnable { vk::False },
			.polygonMode { vk::PolygonMode::eFill },
			.cullMode { vk::CullModeFlagBits::eNone },
			.frontFace { vk::FrontFace::eCounterClockwise },
			.depthBiasEnable { vk::False },
			.lineWidth { 1.0f }
		};

		// Drawn into the same multisampled image as the objects, so with the same samples
		const vk::PipelineMultisampleStateCreateInfo multisample_info {
			.rasterizationSamples { sample_count },
			.sampleShadingEnable { vk::False }
		};

		// Hidden behind the objects, but without writing depth, so the particles never hide each other
		constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_info {
			.depthTestEnable { vk::True },
			.depthWriteEnable { vk::False },
			.depthCompareOp { vk::CompareOp::eLess },
			.depthBoundsTestEnable { vk::False },
			.stencilTestEnable { vk::False }
		};

		// Additive: each particle adds its color weighted by its alpha, so overlapping ones glow and their order doesn't matter
		constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment {
			.blendEnable { vk::True },
			.srcColorBlendFactor { vk::BlendFactor::eSrcAlpha },
			.dstColorBlendFactor { vk::BlendFactor::eOne },
			.colorBlendOp { vk::BlendOp::eAdd },
			// The alpha already in the image is kept as it is
			.srcAlphaBlendFactor { vk::BlendFactor::eZero },
			.dstAlphaBlendFactor { vk::BlendFactor::eOne },
			.alphaBlendOp { vk::BlendOp::eAdd },
			.colorWriteMask {
				vk::ColorComponentFlagBits::eR |
				vk::ColorComponentFlagBits::eG |
				vk::ColorComponentFlagBits::eB |
				vk::ColorComponentFlagBits::eA
			}
		};

		const vk::PipelineColorBlendStateCreateInfo color_blend_info {
			.logicOpEnable { vk::False },
			.attachmentCount { 1 },
			.pAttachments { &color_blend_attachment }
		};

		// Set when recording commands, like for the objects
		constexpr std::array dynamic_states { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

		const vk::PipelineDynamicStateCreateInfo dynamic_state_info {
			.dynamicStateCount { static_cast<std::uint32_t>(dynamic_states.size()) },
			.pDynamicStates { dynamic_states.data() }
		};

		// Drawn in the same rendering as the objects, so with the same attachment formats
		const vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipeline_info_chain {
			{
				.stageCount { static_cast<std::uint32_t>(shader_stages.size()) },
				.pStages { shader_stages.data() },
				.pVertexInputState { &vertex_input_info },
				.pInputAssemblyState { &input_assembly_info },
				.pViewportState { &viewport_state_info },
				.pRasterizationState { &rasterization_info },
				.pMultisampleState { &multisample_info },
				.pDepthStencilState { &depth_stencil_info },
				.pColorBlendState { &color_blend_info },
				.pDynamicState { &dynamic_state_info },
				.layout { *draw_pipeline_layout_ },
				// Not used with dynamic rendering
				.renderPass { nullptr }
			},
			{
				.colorAttachmentCount { 1 },
				.pColorAttachmentFormats { &color_format },
				.depthAttachmentFormat { depth_format }
			}
		};

		draw_pipeline_ = device_.logical_device().createGraphicsPipeline(nullptr, pipeline_info_chain.get<vk::GraphicsPipelineCreateInfo>());
	}
}
