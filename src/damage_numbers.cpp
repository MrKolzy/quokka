#include "quokka/damage_numbers.hpp"

#include "quokka/barrier.hpp"
#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/shader_module.hpp"

#include <algorithm> // std::max, std::min
#include <array> // std::array
#include <cstring> // std::memcpy
#include <span> // std::as_bytes, std::span

namespace quokka
{
	namespace
	{
		// In seconds, must match lifetime in damage_numbers.slang
		constexpr float damage_number_lifetime { 1.0f };

		// Must match max_digits in damage_numbers.slang, with two triangles per digit
		constexpr std::uint32_t max_digits { 6 };
		constexpr std::uint32_t vertices_per_number { max_digits * 6 };

		// The stride of the shader's array, so the C++ struct must take the same space
		static_assert(sizeof(DamageNumber) == 16);

		constexpr vk::DeviceSize ring_size { sizeof(DamageNumber) * DamageNumbers::capacity };

		// The same descriptor set layout as the scene, so the renderer's sets work for both
		vk::raii::PipelineLayout create_pipeline_layout(
			const vk::raii::Device& device,
			const vk::raii::DescriptorSetLayout& descriptor_set_layout)
		{
			const vk::PipelineLayoutCreateInfo pipeline_layout_info {
				.setLayoutCount { 1 },
				.pSetLayouts { &*descriptor_set_layout }
			};

			return device.createPipelineLayout(pipeline_layout_info);
		}

		vk::raii::Pipeline create_pipeline(
			const vk::raii::Device& device,
			const vk::raii::PipelineLayout& pipeline_layout,
			vk::Format color_format)
		{
			const auto shader_code { read_shader_file(constants::damage_number_shader_path) };
			const auto shader_module { create_shader_module(device, shader_code) };

			// Must match the entry points in damage_numbers.slang and in the slangc command
			const std::array shader_stages {
				vk::PipelineShaderStageCreateInfo {
					.stage { vk::ShaderStageFlagBits::eVertex },
					.module { *shader_module },
					.pName { "vert_main" }
				},
				vk::PipelineShaderStageCreateInfo {
					.stage { vk::ShaderStageFlagBits::eFragment },
					.module { *shader_module },
					.pName { "frag_main" }
				}
			};

			// No vertex buffers, the vertex shader builds the quads from its indices
			constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_info {};

			constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_info {
				.topology { vk::PrimitiveTopology::eTriangleList }
			};

			// Only the counts, the actual viewport and scissor are dynamic
			constexpr vk::PipelineViewportStateCreateInfo viewport_state_info {
				.viewportCount { 1 },
				.scissorCount { 1 }
			};

			// Not culled, since the quads always face the screen
			constexpr vk::PipelineRasterizationStateCreateInfo rasterization_info {
				.depthClampEnable { vk::False },
				.rasterizerDiscardEnable { vk::False },
				.polygonMode { vk::PolygonMode::eFill },
				.cullMode { vk::CullModeFlagBits::eNone },
				.frontFace { vk::FrontFace::eCounterClockwise },
				.depthBiasEnable { vk::False },
				.lineWidth { 1.0f }
			};

			// One sample, like the rendering after the MSAA resolve that it's drawn in
			constexpr vk::PipelineMultisampleStateCreateInfo multisample_info {
				.rasterizationSamples { vk::SampleCountFlagBits::e1 },
				.sampleShadingEnable { vk::False }
			};

			// No depth attachment, so numbers are always drawn over the scene
			constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_info {
				.depthTestEnable { vk::False },
				.depthWriteEnable { vk::False },
				.depthBoundsTestEnable { vk::False },
				.stencilTestEnable { vk::False }
			};

			// Blended by alpha, so numbers can fade out
			constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment {
				.blendEnable { vk::True },
				.srcColorBlendFactor { vk::BlendFactor::eSrcAlpha },
				.dstColorBlendFactor { vk::BlendFactor::eOneMinusSrcAlpha },
				.colorBlendOp { vk::BlendOp::eAdd },
				.srcAlphaBlendFactor { vk::BlendFactor::eOne },
				.dstAlphaBlendFactor { vk::BlendFactor::eOneMinusSrcAlpha },
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

			// Set when recording commands, so a resize doesn't need a new pipeline
			constexpr std::array dynamic_states {
				vk::DynamicState::eViewport,
				vk::DynamicState::eScissor
			};

			const vk::PipelineDynamicStateCreateInfo dynamic_state_info {
				.dynamicStateCount { static_cast<std::uint32_t>(dynamic_states.size()) },
				.pDynamicStates { dynamic_states.data() }
			};

			// Straight into the swapchain image, without depth
			const vk::PipelineRenderingCreateInfo rendering_info {
				.colorAttachmentCount { 1 },
				.pColorAttachmentFormats { &color_format }
			};

			const vk::GraphicsPipelineCreateInfo pipeline_info {
				.pNext { &rendering_info },
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
				.layout { *pipeline_layout },
				.renderPass { nullptr }
			};

			return device.createGraphicsPipeline(nullptr, pipeline_info);
		}

		// Every slot starts expired, so nothing shows until the first spawn
		Buffer create_ring_buffer(const Device& device)
		{
			// Only for this upload, which happens once
			const vk::CommandPoolCreateInfo pool_info {
				.flags { vk::CommandPoolCreateFlagBits::eTransient },
				.queueFamilyIndex { device.queue_family_index() }
			};

			const auto command_pool { device.logical_device().createCommandPool(pool_info) };

			// Spawned infinitely long ago, so their age is always past the lifetime
			const std::vector<DamageNumber> expired_numbers(
				DamageNumbers::capacity,
				DamageNumber { .spawn_time { -std::numeric_limits<float>::infinity() } }
			);

			// Also a copy destination, for the numbers each frame uploads
			return create_device_local_buffer(
				device,
				command_pool,
				std::as_bytes(std::span { expired_numbers }),
				vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
			);
		}
	}

	// ***

	DamageNumbers::DamageNumbers(
		const Device& device,
		const vk::raii::DescriptorSetLayout& descriptor_set_layout,
		vk::Format color_format)
		: pipeline_layout_ { create_pipeline_layout(device.logical_device(), descriptor_set_layout) }
		, pipeline_ { create_pipeline(device.logical_device(), pipeline_layout_, color_format) }
		, ring_buffer_ { create_ring_buffer(device) }
	{
		// Host visible to map them, host coherent so the writes need no explicit flushes
		constexpr vk::MemoryPropertyFlags memory_properties {
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		};

		staging_buffers_.reserve(constants::max_frames_in_flight);
		mapped_staging_buffers_.reserve(constants::max_frames_in_flight);

		// As big as the ring, since one frame can spawn that many
		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			staging_buffers_.push_back(create_buffer(
				device,
				ring_size,
				vk::BufferUsageFlagBits::eTransferSrc,
				memory_properties
			));

			// Never unmapped explicitly, freeing the memory unmaps it
			mapped_staging_buffers_.push_back(
				staging_buffers_.back().memory.mapMemory(0, ring_size));
		}

		pending_numbers_.reserve(capacity);
	}

	void DamageNumbers::spawn(const DamageNumber& number)
	{
		// More would overwrite numbers spawned since the same upload
		if (pending_numbers_.size() == capacity)
		{
			return;
		}

		pending_numbers_.push_back(number);
		last_spawn_time_ = std::max(last_spawn_time_, number.spawn_time);
	}

	const Buffer& DamageNumbers::buffer() const noexcept
	{
		return ring_buffer_;
	}

	bool DamageNumbers::has_visible_numbers(float time) const noexcept
	{
		return time - last_spawn_time_ < damage_number_lifetime;
	}

	void DamageNumbers::record_upload(
		const vk::raii::CommandBuffer& command_buffer,
		std::uint32_t frame_index)
	{
		if (pending_numbers_.empty())
		{
			return;
		}

		// The fence wait for this frame guarantees its previous copy is done reading this buffer
		std::memcpy(
			mapped_staging_buffers_[frame_index],
			pending_numbers_.data(),
			pending_numbers_.size() * sizeof(DamageNumber));

		// Up to the end of the ring, and the rest from its start when it wraps around
		const auto count { static_cast<std::uint32_t>(pending_numbers_.size()) };
		const std::uint32_t count_before_wrap { std::min(count, capacity - next_slot_) };

		const std::array regions {
			vk::BufferCopy {
				.srcOffset { 0 },
				.dstOffset { next_slot_ * sizeof(DamageNumber) },
				.size { count_before_wrap * sizeof(DamageNumber) }
			},
			vk::BufferCopy {
				.srcOffset { count_before_wrap * sizeof(DamageNumber) },
				.dstOffset { 0 },
				.size { (count - count_before_wrap) * sizeof(DamageNumber) }
			}
		};
		const std::uint32_t region_count { count_before_wrap < count ? 2u : 1u };

		// Earlier frames may still be drawing from the ring, so the copy waits for their reads
		// Waiting for a read needs no source access, only the stage
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eVertexShader,
			{},
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite
		);

		command_buffer.copyBuffer(
			*staging_buffers_[frame_index].handle,
			*ring_buffer_.handle,
			vk::ArrayProxy<const vk::BufferCopy> { region_count, regions.data() }
		);

		// And this frame's draw waits for the copy
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite,
			vk::PipelineStageFlagBits2::eVertexShader,
			vk::AccessFlagBits2::eShaderStorageRead
		);

		next_slot_ = (next_slot_ + count) % capacity;
		pending_numbers_.clear();
	}

	void DamageNumbers::record_draw(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		vk::Extent2D extent) const
	{
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_);

		// Flipped like the scene's, so clip space y points up for the projection and the offsets
		const vk::Viewport viewport {
			.x { 0.0f },
			.y { static_cast<float>(extent.height) },
			.width { static_cast<float>(extent.width) },
			.height { -static_cast<float>(extent.height) },
			.minDepth { 0.0f },
			.maxDepth { 1.0f }
		};

		command_buffer.setViewport(0, viewport);
		command_buffer.setScissor(0, vk::Rect2D { .offset { 0, 0 }, .extent { extent } });

		// Set 0, with no dynamic offsets
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*pipeline_layout_,
			0,
			*descriptor_set,
			{}
		);

		// Every slot of the ring, and the vertex shader drops expired numbers and unused digits
		command_buffer.draw(vertices_per_number, capacity, 0, 0);
	}
}
