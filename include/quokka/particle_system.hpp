#pragma once

#include "quokka/buffer.hpp"
#include "quokka/device.hpp"
#include "quokka/graphics_pipeline.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector>

namespace quokka
{
	// Particles moved by a compute shader and drawn as points, with no work on the CPU after the initial upload
	class ParticleSystem
	{
	public:
		// The particles are drawn with the camera of the graphics pipeline, into attachments with the given formats and samples
		ParticleSystem(
			const Device& device,
			const GraphicsPipeline& graphics_pipeline,
			vk::Format color_format,
			vk::Format depth_format,
			vk::SampleCountFlagBits sample_count);

		// Neither copyable nor movable: the renderer keeps a reference to it
		ParticleSystem(const ParticleSystem&) = delete;
		ParticleSystem& operator=(const ParticleSystem&) = delete;
		ParticleSystem(ParticleSystem&&) = delete;
		ParticleSystem& operator=(ParticleSystem&&) = delete;

		// Moves the particles of this frame from where they were in the previous one, outside of any rendering
		void record_update(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index, float delta_seconds) const;

		// Draws the particles of this frame, inside a rendering whose viewport and scissor are already set
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

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from
		// One per frame in flight, written by the update of that frame and read as vertices to draw it
		std::vector<Buffer> storage_buffers_ {};

		// The particles of the previous frame to read and those of the current one to write
		vk::raii::DescriptorSetLayout update_descriptor_set_layout_ { nullptr };
		// Allocates the memory of the update descriptor sets
		vk::raii::DescriptorPool descriptor_pool_ { nullptr };
		// One per frame in flight, each pointing to the storage buffers of that frame and of the previous one
		std::vector<vk::raii::DescriptorSet> update_descriptor_sets_ {};

		vk::raii::PipelineLayout update_pipeline_layout_ { nullptr };
		// Only the compute shader, without any of the fixed-function state of a graphics pipeline
		vk::raii::Pipeline update_pipeline_ { nullptr };

		vk::raii::PipelineLayout draw_pipeline_layout_ { nullptr };
		// Draws each particle as a point, blended additively on top of the objects
		vk::raii::Pipeline draw_pipeline_ { nullptr };
	};
}
