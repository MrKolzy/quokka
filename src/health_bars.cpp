#include "quokka/health_bars.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/overlay_pipeline.hpp"

namespace quokka
{
	namespace
	{
		// The scene's frame set layout, so the renderer's frame sets work here too
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
	}

	// ***

	HealthBars::HealthBars(
		const Device& device,
		const vk::raii::DescriptorSetLayout& descriptor_set_layout,
		vk::Format color_format)
		: pipeline_layout_ {
			create_pipeline_layout(device.logical_device(), descriptor_set_layout)
		}
		, pipeline_ {
			create_overlay_pipeline(
				device.logical_device(),
				pipeline_layout_,
				color_format,
				constants::health_bar_shader_path,
				"vert_main",
				"frag_main")
		}
	{
	}

	void HealthBars::record_draw(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		vk::Extent2D extent) const
	{
		set_overlay_viewport(command_buffer, extent);

		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_);

		// Set 0, with no dynamic offsets
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*pipeline_layout_,
			0,
			*descriptor_set,
			{}
		);

		// A quad per enemy, and the vertex shader drops the ones that need no bar
		command_buffer.draw(6, constants::enemy_count, 0, 0);
	}
}
