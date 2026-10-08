#pragma once

#include "quokka/buffer.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector> // std::vector

namespace quokka
{
	class Device;
	class GraphicsPipeline;

	class ParticleSystem
	{
	public:
		// Takes graphics_pipeline to reuse its camera descriptor set layout
		ParticleSystem(
			const Device& device,
			const GraphicsPipeline& graphics_pipeline,
			vk::Format color_format,
			vk::Format depth_format,
			vk::SampleCountFlagBits sample_count);
		~ParticleSystem() = default;

		// Renderer keeps a reference to it, so it can't be copied or moved
		ParticleSystem(const ParticleSystem&) = delete;
		ParticleSystem& operator=(const ParticleSystem&) = delete;
		ParticleSystem(ParticleSystem&&) = delete;
		ParticleSystem& operator=(ParticleSystem&&) = delete;

		// Must be recorded outside any rendering, since it's compute work
		void record_update(
			const vk::raii::CommandBuffer& command_buffer,
			std::uint32_t frame_index,
			float delta_seconds) const;

		// Must be recorded inside a rendering with the viewport and scissor already set
		void record_draw(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& camera_descriptor_set,
			std::uint32_t frame_index) const;

	private:
		void create_storage_buffers();
		void create_update_descriptor_set_layout();
		void create_descriptor_pool();
		void create_update_descriptor_sets();
		void create_update_pipeline(const vk::raii::ShaderModule& shader_module);
		void create_draw_pipeline(
			const vk::raii::ShaderModule& shader_module,
			const vk::raii::DescriptorSetLayout& camera_descriptor_set_layout,
			vk::Format color_format,
			vk::Format depth_format,
			vk::SampleCountFlagBits sample_count);

		const Device& device_;

		// Each one is destroyed before the object it came from, so don't reorder
		// One per frame in flight, written by that frame's update and read as vertices to draw it
		std::vector<Buffer> storage_buffers_ {};

		vk::raii::DescriptorSetLayout update_descriptor_set_layout_ { nullptr };
		vk::raii::DescriptorPool descriptor_pool_ { nullptr };

		// One per frame in flight, because each points to its frame's buffer and the previous one
		std::vector<vk::raii::DescriptorSet> update_descriptor_sets_ {};

		vk::raii::PipelineLayout update_pipeline_layout_ { nullptr };
		vk::raii::Pipeline update_pipeline_ { nullptr };

		vk::raii::PipelineLayout draw_pipeline_layout_ { nullptr };
		vk::raii::Pipeline draw_pipeline_ { nullptr };
	};
}
