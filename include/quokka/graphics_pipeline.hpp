#pragma once

#include "quokka/device.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	// Owns the graphics pipeline and its layout, built once for a given color format
	class GraphicsPipeline
	{
	public:
		GraphicsPipeline(const Device& device, vk::Format color_format);

		[[nodiscard]] const vk::raii::Pipeline& handle() const noexcept;

	private:
		// Declared in creation order: members are initialized in this order and destroyed in reverse
		// Descriptor set layouts and push constant ranges the shaders can access
		vk::raii::PipelineLayout pipeline_layout_;
		// Shader stages and fixed-function state used to draw
		vk::raii::Pipeline graphics_pipeline_;
	};
}
