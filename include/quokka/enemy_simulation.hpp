#pragma once

#include "quokka/abilities.hpp"
#include "quokka/buffer.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector> // std::vector

namespace quokka
{
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

		// Dead at 0 or below
		float health;

		// In seconds since the start, and for dead enemies the moment they died
		float last_hit_time;

		// In seconds since the start, when it can attack the player again
		float next_attack_time;

		// The shader's struct holds a float2, so its size is a multiple of 8
		float padding;
	};

	// The stride of the shader's array, so the C++ struct must take the same space
	static_assert(sizeof(EnemyState) == 32);

	class EnemySimulation
	{
	public:
		// The hits spawn their damage numbers into the ring of damage_number_buffer
		EnemySimulation(const Device& device, const Buffer& damage_number_buffer);
		~EnemySimulation() = default;

		// Renderer keeps a reference to it, so it can't be copied or moved
		EnemySimulation(const EnemySimulation&) = delete;
		EnemySimulation& operator=(const EnemySimulation&) = delete;
		EnemySimulation(EnemySimulation&&) = delete;
		EnemySimulation& operator=(EnemySimulation&&) = delete;

		// Past a corner of the grid the enemies start in, as world x and z, out of their reach
		// so nobody chases the player until it walks closer
		[[nodiscard]] static glm::vec2 player_start() noexcept;

		// For the next record_update, the points are on the ground as world x and z
		// The enemies chase the target and attack it while it's alive, and the aim point picks
		// who the strike hits
		void prepare_update(
			glm::vec2 target,
			glm::vec2 aim_point,
			bool is_target_alive,
			float delta_seconds,
			float time_seconds) noexcept;

		// During the next record_update, the blast hits everyone around the aim point
		// and the strike hits only the enemy closest to it
		void cast(Ability ability) noexcept;

		// During the next record_update, a damage number on every enemy without hurting anyone,
		// so a benchmark can show as many numbers as there are enemies again and again
		void cast_test_hits() noexcept;

		// Puts every enemy back where it started, during the next record_update
		void reset() noexcept;

		// Read by the vertex shaders, so they go into the renderer's descriptor sets
		// The combat state holds the target and the total damage
		[[nodiscard]] const Buffer& state_buffer() const noexcept;
		[[nodiscard]] const Buffer& combat_buffer() const noexcept;

		// Once this frame in flight's fence is signaled, reads the damage its last update dealt
		// to the target
		void collect(std::uint32_t frame_index) noexcept;

		// The damage collected since the last call, so each attack is taken once
		[[nodiscard]] std::uint32_t take_player_damage() noexcept;

		// Outside a rendering, and before anything this frame reads the enemies
		void record_update(
			const vk::raii::CommandBuffer& command_buffer,
			std::uint32_t frame_index);

	private:
		void create_buffers(const Device& device);
		void create_descriptor_set(const Device& device, const Buffer& damage_number_buffer);

		// Between dispatches, so each one reads what the previous one wrote
		void record_compute_to_compute_barrier(const vk::raii::CommandBuffer& command_buffer) const;

		// Each member uses the ones above it and is destroyed before them, so don't reorder
		vk::raii::DescriptorSetLayout descriptor_set_layout_;
		vk::raii::PipelineLayout pipeline_layout_;
		vk::raii::Pipeline update_pipeline_;
		vk::raii::Pipeline strike_pipeline_;
		vk::raii::Pipeline tally_pipeline_;

		vk::raii::DescriptorPool descriptor_pool_ { nullptr };

		// A single set, since the buffers are the same every frame and barriers order their use
		vk::raii::DescriptorSet descriptor_set_ { nullptr };

		// The grid they start in, copied back by a reset
		Buffer initial_states_ {};

		// Updated in place every frame
		Buffer states_ {};

		// What states_ held before this frame's update, so neighbors are read before they move
		Buffer previous_states_ {};

		// The target picked this frame, which the vertex shaders highlight, and the total damage
		Buffer combat_ {};

		// One per frame in flight, the damage dealt to the player copied for the CPU to read
		std::vector<Buffer> readback_buffers_ {};
		std::vector<void*> mapped_readbacks_ {};

		// Collected but not taken yet
		std::uint32_t player_damage_ {};

		glm::vec2 target_ {};
		glm::vec2 aim_point_ {};
		bool is_target_alive_ { true };
		float delta_seconds_ {};
		float time_seconds_ {};
		bool should_reset_ { false };

		// Cleared after each record_update, so every cast hits once
		bool is_blast_cast_ { false };
		bool is_strike_cast_ { false };
		bool is_test_cast_ { false };
	};
}
