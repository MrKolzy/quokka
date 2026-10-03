#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	// Owns the graphics pipeline and the layouts of the resources it accesses, built once for given attachment formats and sample count
	class GraphicsPipeline
	{
	public:
		GraphicsPipeline(const Device& device, vk::Format color_format, vk::Format depth_format, vk::SampleCountFlagBits sample_count);

		[[nodiscard]] const vk::raii::DescriptorSetLayout& descriptor_set_layout() const noexcept;
		[[nodiscard]] const vk::raii::PipelineLayout& pipeline_layout() const noexcept;
		[[nodiscard]] const vk::raii::Pipeline& handle() const noexcept;

	private:
		// Declared in creation order: members are initialized in this order and destroyed in reverse
		// Types and bindings of the resources the shaders access through descriptor sets
		vk::raii::DescriptorSetLayout descriptor_set_layout_;
		// Descriptor set layouts and push constant ranges the shaders can access
		vk::raii::PipelineLayout pipeline_layout_;
		// Shader stages and fixed-function state used to draw
		vk::raii::Pipeline graphics_pipeline_;
	};
}
