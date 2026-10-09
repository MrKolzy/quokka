#include "quokka/overlay_pipeline.hpp"

#include "quokka/shader_module.hpp"

#include <array> // std::array
#include <cstdint> // std::uint32_t

namespace quokka
{
	vk::raii::Pipeline create_overlay_pipeline(
		const vk::raii::Device& device,
		const vk::raii::PipelineLayout& pipeline_layout,
		vk::Format color_format,
		const char* shader_path,
		const char* vertex_entry_point,
		const char* fragment_entry_point)
	{
		const auto shader_code { read_shader_file(shader_path) };
		const auto shader_module { create_shader_module(device, shader_code) };

		// Must match the entry points in the shader and in its slangc command
		const std::array shader_stages {
			vk::PipelineShaderStageCreateInfo {
				.stage { vk::ShaderStageFlagBits::eVertex },
				.module { *shader_module },
				.pName { vertex_entry_point }
			},
			vk::PipelineShaderStageCreateInfo {
				.stage { vk::ShaderStageFlagBits::eFragment },
				.module { *shader_module },
				.pName { fragment_entry_point }
			}
		};

		// No vertex buffers, the vertex shaders build their quads from the vertex indices
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

		// No depth attachment, so everything is drawn over the scene
		constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_info {
			.depthTestEnable { vk::False },
			.depthWriteEnable { vk::False },
			.depthBoundsTestEnable { vk::False },
			.stencilTestEnable { vk::False }
		};

		// Blended by alpha, so things can fade out
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

	void set_overlay_viewport(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent)
	{
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
	}
}