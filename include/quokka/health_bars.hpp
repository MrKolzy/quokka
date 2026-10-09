#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	class Device;

	class HealthBars
	{
	public:
		HealthBars(
			const Device& device,
			const vk::raii::DescriptorSetLayout& descriptor_set_layout,
			vk::Format color_format);
		~HealthBars() = default;

		// Renderer keeps a reference to it, so it can't be copied or moved
		HealthBars(const HealthBars&) = delete;
		HealthBars& operator=(const HealthBars&) = delete;
		HealthBars(HealthBars&&) = delete;
		HealthBars& operator=(HealthBars&&) = delete;

		// Inside a rendering with one sample and no depth, after this frame's simulation
		void record_draw(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			vk::Extent2D extent) const;

	private:
		// Each member uses the ones above it and is destroyed before them, so don't reorder
		vk::raii::PipelineLayout pipeline_layout_;
		vk::raii::Pipeline pipeline_;
	};
}
