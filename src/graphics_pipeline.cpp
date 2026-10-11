#include "quokka/graphics_pipeline.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/shader_module.hpp"
#include "quokka/vertex.hpp"

#include <array> // std::array
#include <cstdint> // std::uint32_t

namespace quokka
{
	namespace
	{
		vk::raii::DescriptorSetLayout create_descriptor_set_layout(const vk::raii::Device& device)
		{
			// Must match frame in shader.slang
			// The fragment shader also reads it, for the light and the camera position
			constexpr vk::DescriptorSetLayoutBinding frame_binding {
				.binding { 0 },
				.descriptorType { vk::DescriptorType::eUniformBuffer },
				.descriptorCount { 1 },
				.stageFlags {
					vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
				}
			};

			// Must match texture in shader.slang
			constexpr vk::DescriptorSetLayoutBinding texture_binding {
				.binding { 1 },
				.descriptorType { vk::DescriptorType::eCombinedImageSampler },
				.descriptorCount { 1 },
				.stageFlags { vk::ShaderStageFlagBits::eFragment }
			};

			constexpr std::array bindings { frame_binding, texture_binding };

			const vk::DescriptorSetLayoutCreateInfo create_info {
				.bindingCount { static_cast<std::uint32_t>(bindings.size()) },
				.pBindings { bindings.data() }
			};

			return device.createDescriptorSetLayout(create_info);
		}

		vk::raii::PipelineLayout create_pipeline_layout(
			const vk::raii::Device& device,
			const vk::raii::DescriptorSetLayout& descriptor_set_layout)
		{
			// Pushed before each draw, so more objects need no extra buffers or descriptor sets
			constexpr vk::PushConstantRange object_constants_range {
				.stageFlags { vk::ShaderStageFlagBits::eVertex },
				.offset { 0 },
				.size { sizeof(ObjectConstants) }
			};

			// A single descriptor set, set 0 in the shader
			const vk::PipelineLayoutCreateInfo pipeline_layout_info {
				.setLayoutCount { 1 },
				.pSetLayouts { &*descriptor_set_layout },
				.pushConstantRangeCount { 1 },
				.pPushConstantRanges { &object_constants_range }
			};

			return device.createPipelineLayout(pipeline_layout_info);
		}

		vk::raii::Pipeline create_graphics_pipeline(
			const vk::raii::Device& device,
			const vk::raii::PipelineLayout& pipeline_layout,
			vk::Format color_format,
			vk::Format depth_format,
			vk::SampleCountFlagBits sample_count)
		{
			// Both entry points live in the same compiled module
			const auto shader_code { read_shader_file(constants::shader_path) };
			const auto shader_module { create_shader_module(device, shader_code) };

			// Must match the entry points in shader.slang and in the slangc command
			const vk::PipelineShaderStageCreateInfo vertex_shader_stage_info {
				.stage { vk::ShaderStageFlagBits::eVertex },
				.module { *shader_module },
				.pName { "vert_main" }
			};

			const vk::PipelineShaderStageCreateInfo fragment_shader_stage_info {
				.stage { vk::ShaderStageFlagBits::eFragment },
				.module { *shader_module },
				.pName { "frag_main" }
			};

			const std::array shader_stages { vertex_shader_stage_info, fragment_shader_stage_info };

			// Only the layout, the vertex buffer itself is bound when recording commands
			constexpr auto binding_description { Vertex::binding_description() };
			constexpr auto attribute_descriptions { Vertex::attribute_descriptions() };

			const vk::PipelineVertexInputStateCreateInfo vertex_input_info {
				.vertexBindingDescriptionCount { 1 },
				.pVertexBindingDescriptions { &binding_description },
				.vertexAttributeDescriptionCount {
					static_cast<std::uint32_t>(attribute_descriptions.size())
				},
				.pVertexAttributeDescriptions { attribute_descriptions.data() }
			};

			constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_info {
				.topology { vk::PrimitiveTopology::eTriangleList }
			};

			// Only the counts, the actual viewport and scissor are dynamic
			constexpr vk::PipelineViewportStateCreateInfo viewport_state_info {
				.viewportCount { 1 },
				.scissorCount { 1 }
			};

			constexpr vk::PipelineRasterizationStateCreateInfo rasterization_info {
				// Clamping fragments outside the near and far planes requires a GPU feature
				.depthClampEnable { vk::False },
				.rasterizerDiscardEnable { vk::False },
				// Any mode other than fill requires a GPU feature
				.polygonMode { vk::PolygonMode::eFill },
				.cullMode { vk::CullModeFlagBits::eBack },
				// GLM's convention, kept on screen by the flipped viewport
				.frontFace { vk::FrontFace::eCounterClockwise },
				// Mainly used for shadow mapping
				.depthBiasEnable { vk::False },
				// Any width other than 1.0 requires a GPU feature
				.lineWidth { 1.0f }
			};

			const vk::PipelineMultisampleStateCreateInfo multisample_info {
				.rasterizationSamples { sample_count },
				// Off, so the fragment shader runs once per pixel instead of once per sample
				.sampleShadingEnable { vk::False }
			};

			constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_info {
				.depthTestEnable { vk::True },
				.depthWriteEnable { vk::True },
				// Smaller depth is closer, and the depth image is cleared to 1.0, the far plane
				.depthCompareOp { vk::CompareOp::eLess },
				.depthBoundsTestEnable { vk::False },
				.stencilTestEnable { vk::False }
			};

			constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment {
				.blendEnable { vk::False },
				// Every channel, because an empty mask would write nothing
				.colorWriteMask {
					vk::ColorComponentFlagBits::eR |
					vk::ColorComponentFlagBits::eG |
					vk::ColorComponentFlagBits::eB |
					vk::ColorComponentFlagBits::eA
				}
			};

			// One blend attachment per color attachment, and there's only one
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

			// No render pass with dynamic rendering, so the attachment formats go here
			const vk::PipelineRenderingCreateInfo rendering_info {
				// Only the multisampled image, which uses the swapchain's format
				.colorAttachmentCount { 1 },
				.pColorAttachmentFormats { &color_format },
				// No stencil, so stencilAttachmentFormat stays undefined
				.depthAttachmentFormat { depth_format }
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

			// No pipeline cache yet, which would reuse compiled data across creations and runs
			return device.createGraphicsPipeline(nullptr, pipeline_info);
		}
	}

	// ***

	GraphicsPipeline::GraphicsPipeline(
		const Device& device,
		vk::Format color_format,
		vk::Format depth_format,
		vk::SampleCountFlagBits sample_count)
		: descriptor_set_layout_ { create_descriptor_set_layout(device.logical_device()) }
		, pipeline_layout_ {
			create_pipeline_layout(device.logical_device(), descriptor_set_layout_)
		}
		, graphics_pipeline_ {
			create_graphics_pipeline(
				device.logical_device(),
				pipeline_layout_,
				color_format,
				depth_format,
				sample_count)
		}
	{
	}

	const vk::raii::DescriptorSetLayout& GraphicsPipeline::descriptor_set_layout() const noexcept
	{
		return descriptor_set_layout_;
	}

	const vk::raii::PipelineLayout& GraphicsPipeline::pipeline_layout() const noexcept
	{
		return pipeline_layout_;
	}

	const vk::raii::Pipeline& GraphicsPipeline::handle() const noexcept
	{
		return graphics_pipeline_;
	}
}
