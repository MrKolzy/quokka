#include "quokka/graphics_pipeline.hpp"

#include "quokka/constants.hpp"
#include "quokka/vertex.hpp"

#include <array>
#include <cstddef> // std::size_t
#include <cstdint> // std::uint32_t
#include <filesystem>
#include <fstream>
#include <stdexcept> // std::runtime_error
#include <string>
#include <vector>

namespace quokka
{
	namespace
	{
		// Reads a compiled SPIR-V file as 32-bit words, which also guarantees the alignment Vulkan expects
		std::vector<std::uint32_t> read_shader_file(const std::filesystem::path& path)
		{
			// Start at the end (ate) to get the file size, and read raw bytes without text conversions (binary)
			std::ifstream file { path, std::ios::ate | std::ios::binary };
			if (!file.is_open())
			{
				throw std::runtime_error("Failed to open shader file: " + path.string());
			}

			const auto file_size { static_cast<std::size_t>(file.tellg()) };

			// SPIR-V is a stream of 32-bit words, so an empty file or a partial word means it's invalid
			if (file_size == 0 || file_size % sizeof(std::uint32_t) != 0)
			{
				throw std::runtime_error("Invalid SPIR-V file size: " + path.string());
			}

			// Parentheses, not braces: this is the count constructor
			std::vector<std::uint32_t> code(file_size / sizeof(std::uint32_t));

			file.seekg(0, std::ios::beg);
			file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(file_size));

			if (!file)
			{
				throw std::runtime_error("Failed to read shader file: " + path.string());
			}

			return code;
		}

		// Wraps SPIR-V code in a shader module, only needed until the pipeline is created
		vk::raii::ShaderModule create_shader_module(const vk::raii::Device& device, const std::vector<std::uint32_t>& code)
		{
			const vk::ShaderModuleCreateInfo create_info {
				// Size in bytes, not in words
				.codeSize { code.size() * sizeof(std::uint32_t) },
				.pCode { code.data() }
			};

			return device.createShaderModule(create_info);
		}

		vk::raii::DescriptorSetLayout create_descriptor_set_layout(const vk::raii::Device& device)
		{
			// The transformation matrices, read only by the vertex shader
			constexpr vk::DescriptorSetLayoutBinding transforms_binding {
				.binding { 0 },
				.descriptorType { vk::DescriptorType::eUniformBuffer },
				// A single uniform buffer, not an array of them
				.descriptorCount { 1 },
				.stageFlags { vk::ShaderStageFlagBits::eVertex }
			};

			// The texture and its sampler, read only by the fragment shader
			constexpr vk::DescriptorSetLayoutBinding texture_binding {
				.binding { 1 },
				.descriptorType { vk::DescriptorType::eCombinedImageSampler },
				.descriptorCount { 1 },
				.stageFlags { vk::ShaderStageFlagBits::eFragment }
			};

			constexpr std::array bindings { transforms_binding, texture_binding };

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
			// A single descriptor set, set 0 in the shader, and no push constants yet
			const vk::PipelineLayoutCreateInfo pipeline_layout_info {
				.setLayoutCount { 1 },
				.pSetLayouts { &*descriptor_set_layout }
			};

			return device.createPipelineLayout(pipeline_layout_info);
		}

		vk::raii::Pipeline create_graphics_pipeline(
			const vk::raii::Device& device,
			const vk::raii::PipelineLayout& pipeline_layout,
			vk::Format color_format)
		{
			// Both entry points live in the same compiled module
			const auto shader_module { create_shader_module(device, read_shader_file(constants::shader_path)) };

			// Must match the entry point names in the shader and in the slangc command
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

			// Only the layout of the vertex data, the buffer itself is bound when recording commands
			constexpr auto binding_description { Vertex::binding_description() };
			constexpr auto attribute_descriptions { Vertex::attribute_descriptions() };

			const vk::PipelineVertexInputStateCreateInfo vertex_input_info {
				.vertexBindingDescriptionCount { 1 },
				.pVertexBindingDescriptions { &binding_description },
				.vertexAttributeDescriptionCount { static_cast<std::uint32_t>(attribute_descriptions.size()) },
				.pVertexAttributeDescriptions { attribute_descriptions.data() }
			};

			// Every three vertices form an independent triangle
			constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_info {
				.topology { vk::PrimitiveTopology::eTriangleList }
			};

			// Only the counts, the actual viewport and scissor are dynamic
			constexpr vk::PipelineViewportStateCreateInfo viewport_state_info {
				.viewportCount { 1 },
				.scissorCount { 1 }
			};

			// Turns the triangle into fragments, filling it and discarding the back faces
			constexpr vk::PipelineRasterizationStateCreateInfo rasterization_info {
				// Clamping fragments outside the near and far planes requires a GPU feature
				.depthClampEnable { vk::False },
				// Otherwise geometry would never reach the framebuffer
				.rasterizerDiscardEnable { vk::False },
				// Any mode other than fill requires a GPU feature
				.polygonMode { vk::PolygonMode::eFill },
				.cullMode { vk::CullModeFlagBits::eBack },
				// Counterclockwise on screen, the convention GLM's matrices assume, kept by the flipped viewport
				.frontFace { vk::FrontFace::eCounterClockwise },
				// Mainly used for shadow mapping
				.depthBiasEnable { vk::False },
				// Any width other than 1.0 requires a GPU feature
				.lineWidth { 1.0f }
			};

			// Multisampling disabled, one sample per pixel
			constexpr vk::PipelineMultisampleStateCreateInfo multisample_info {
				.rasterizationSamples { vk::SampleCountFlagBits::e1 },
				.sampleShadingEnable { vk::False }
			};

			// No blending, the fragment color overwrites the framebuffer, but every channel is still written
			constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment {
				.blendEnable { vk::False },
				.colorWriteMask {
					vk::ColorComponentFlagBits::eR |
					vk::ColorComponentFlagBits::eG |
					vk::ColorComponentFlagBits::eB |
					vk::ColorComponentFlagBits::eA
				}
			};

			// One blend attachment per color attachment, the swapchain image is the only one
			const vk::PipelineColorBlendStateCreateInfo color_blend_info {
				.logicOpEnable { vk::False },
				.attachmentCount { 1 },
				.pAttachments { &color_blend_attachment }
			};

			// Pipelines are immutable, so changing any state means creating a new one
			// Viewport and scissor are set when recording commands instead, so a resize doesn't require a new pipeline
			constexpr std::array dynamic_states { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

			const vk::PipelineDynamicStateCreateInfo dynamic_state_info {
				.dynamicStateCount { static_cast<std::uint32_t>(dynamic_states.size()) },
				.pDynamicStates { dynamic_states.data() }
			};

			// Dynamic rendering replaces the render pass and framebuffers, so the pipeline only needs the attachment formats
			// The chain links the rendering info through the pNext of the pipeline create info
			const vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipeline_info_chain {
				{
					.stageCount { static_cast<std::uint32_t>(shader_stages.size()) },
					.pStages { shader_stages.data() },
					.pVertexInputState { &vertex_input_info },
					.pInputAssemblyState { &input_assembly_info },
					.pViewportState { &viewport_state_info },
					.pRasterizationState { &rasterization_info },
					.pMultisampleState { &multisample_info },
					.pColorBlendState { &color_blend_info },
					.pDynamicState { &dynamic_state_info },
					.layout { *pipeline_layout },
					// Not used with dynamic rendering
					.renderPass { nullptr }
				},
				{
					// A single color attachment, the swapchain image, with its exact format
					.colorAttachmentCount { 1 },
					.pColorAttachmentFormats { &color_format }
				}
			};

			// No pipeline cache yet, it would let Vulkan reuse data across pipeline creations and program runs
			return device.createGraphicsPipeline(nullptr, pipeline_info_chain.get<vk::GraphicsPipelineCreateInfo>());
		}
	}

	// ***

	GraphicsPipeline::GraphicsPipeline(const Device& device, vk::Format color_format)
		: descriptor_set_layout_ { create_descriptor_set_layout(device.logical_device()) }
		, pipeline_layout_ { create_pipeline_layout(device.logical_device(), descriptor_set_layout_) }
		, graphics_pipeline_ { create_graphics_pipeline(device.logical_device(), pipeline_layout_, color_format) }
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
