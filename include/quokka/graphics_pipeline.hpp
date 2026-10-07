#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	class Device;

	struct ObjectConstants
	{
		// Must match ObjectConstants in shader.slang
		// 64 bytes, well within the 128 bytes of push constants every GPU guarantees
		glm::mat4 model;
	};

	class GraphicsPipeline
	{
	public:
		GraphicsPipeline(
			const Device& device,
			vk::Format color_format,
			vk::Format depth_format,
			vk::SampleCountFlagBits sample_count);
		~GraphicsPipeline() = default;

		// Other modules keep references to it, so it can't be copied or moved
		GraphicsPipeline(const GraphicsPipeline&) = delete;
		GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
		GraphicsPipeline(GraphicsPipeline&&) = delete;
		GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

		[[nodiscard]] const vk::raii::DescriptorSetLayout& descriptor_set_layout() const noexcept;
		[[nodiscard]] const vk::raii::PipelineLayout& pipeline_layout() const noexcept;
		[[nodiscard]] const vk::raii::Pipeline& handle() const noexcept;

	private:
		// Each member uses the ones above it and is destroyed before them, so don't reorder
		vk::raii::DescriptorSetLayout descriptor_set_layout_;
		vk::raii::PipelineLayout pipeline_layout_;
		vk::raii::Pipeline graphics_pipeline_;
	};
}
