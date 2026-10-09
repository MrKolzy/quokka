#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	// For what's drawn over the finished scene, such as damage numbers and health bars,
	// in a rendering with one sample and no depth, and without vertex buffers
	[[nodiscard]] vk::raii::Pipeline create_overlay_pipeline(
		const vk::raii::Device& device,
		const vk::raii::PipelineLayout& pipeline_layout,
		vk::Format color_format,
		const char* shader_path,
		const char* vertex_entry_point,
		const char* fragment_entry_point);

	// The whole extent, flipped like the scene's viewport, which the overlay pipelines expect
	void set_overlay_viewport(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent);
}