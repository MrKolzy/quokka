#pragma once

#include "quokka/buffer.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t

namespace quokka
{
	class Camera;
	class Device;

	struct EnemyState
	{
		// Must match EnemyState in enemy.slang
		// On the ground, as world x and z
		glm::vec2 position;

		// In radians around the world up axis, 0 faces +z and positive values turn toward +x
		float yaw;

		// 0 or 1, since a bool has no fixed size in a shader buffer
		std::uint32_t is_chasing;
	};

	class EnemySimulation
	{
	public:
		explicit EnemySimulation(const Device& device);
		~EnemySimulation() = default;

		// Renderer keeps a reference to it, so it can't be copied or moved
		EnemySimulation(const EnemySimulation&) = delete;
		EnemySimulation& operator=(const EnemySimulation&) = delete;
		EnemySimulation(EnemySimulation&&) = delete;
		EnemySimulation& operator=(EnemySimulation&&) = delete;

		// Frames the whole grid the enemies start in
		[[nodiscard]] static Camera initial_camera() noexcept;

		// For the next record_update, the target is a point on the ground as world x and z
		void prepare_update(glm::vec2 target, float delta_seconds) noexcept;

		// Puts every enemy back where it started, during the next record_update
		void reset() noexcept;

		// Read by the vertex shaders, so it goes into the renderer's descriptor sets
		[[nodiscard]] const Buffer& state_buffer() const noexcept;

		// Outside a rendering, and before anything this frame reads the enemies
		void record_update(const vk::raii::CommandBuffer& command_buffer);

	private:
		void create_buffers(const Device& device);
		void create_descriptor_set(const Device& device);

		// Each member uses the ones above it and is destroyed before them, so don't reorder
		vk::raii::DescriptorSetLayout descriptor_set_layout_;
		vk::raii::PipelineLayout pipeline_layout_;
		vk::raii::Pipeline pipeline_;

		vk::raii::DescriptorPool descriptor_pool_ { nullptr };

		// A single set, since the buffers are the same every frame and barriers order their use
		vk::raii::DescriptorSet descriptor_set_ { nullptr };

		// The grid they start in, copied back by a reset
		Buffer initial_states_ {};

		// Updated in place every frame
		Buffer states_ {};

		// What states_ held before this frame's update, so neighbors are read before they move
		Buffer previous_states_ {};

		glm::vec2 target_ {};
		float delta_seconds_ {};
		bool should_reset_ { false };
	};
}
