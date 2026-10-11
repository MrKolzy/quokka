#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t

namespace quokka
{
	class Device;

	struct PlayerConstants
	{
		// Must match PlayerConstants in shader.slang
		// On the ground as world x and z, with the yaw convention of EnemyState
		glm::vec2 position;
		float yaw;

		// In seconds since the start, for the flash like EnemyState
		float last_hit_time;

		// An AnimationClip, and how far into it the player is, in seconds
		std::uint32_t animation_clip;
		float animation_time;

		// In seconds since the start, when the ground last marked how far the sword reaches
		float strike_range_time;
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

		// Set 0, one per frame in flight, with what changes every frame or is shared by every draw
		[[nodiscard]] const vk::raii::DescriptorSetLayout& frame_descriptor_set_layout()
			const noexcept;

		// Set 1, one per model, with its texture and its animations
		[[nodiscard]] const vk::raii::DescriptorSetLayout& material_descriptor_set_layout()
			const noexcept;

		// Shared by every pipeline, so the descriptor sets stay bound when switching between them
		[[nodiscard]] const vk::raii::PipelineLayout& pipeline_layout() const noexcept;

		// The enemies are instances that read their state from the simulation's buffer
		[[nodiscard]] const vk::raii::Pipeline& enemy_pipeline() const noexcept;

		// The player is a single draw whose state comes in PlayerConstants
		[[nodiscard]] const vk::raii::Pipeline& player_pipeline() const noexcept;

		// A square around the camera, with no vertex buffer, that also marks the blast circle
		[[nodiscard]] const vk::raii::Pipeline& ground_pipeline() const noexcept;

	private:
		// Each member uses the ones above it and is destroyed before them, so don't reorder
		vk::raii::DescriptorSetLayout frame_descriptor_set_layout_;
		vk::raii::DescriptorSetLayout material_descriptor_set_layout_;
		vk::raii::PipelineLayout pipeline_layout_;
		vk::raii::Pipeline enemy_pipeline_;
		vk::raii::Pipeline player_pipeline_;
		vk::raii::Pipeline ground_pipeline_;
	};
}
