#pragma once

#include "quokka/abilities.hpp"
#include "quokka/buffer.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstddef> // std::size_t
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

		// Bits of chasing_flag and moving_flag in enemy.slang
		std::uint32_t flags;

		// Dead at 0 or below
		float health;

		// In seconds since the start, and for dead enemies the moment they died
		float last_hit_time;

		// In seconds since the start, when it can attack the player again and when it last did
		float next_attack_time;
		float last_attack_time;

		// In world units per second, the push of the last hits away from the player, which fades
		glm::vec2 knockback;
	};

	// The stride of the shader's array, so the C++ struct must take the same space
	static_assert(sizeof(EnemyState) == 40);

	struct SelectedEnemy
	{
		std::uint32_t index { 0xFFFFFFFF };
		float health {};

		// On the ground, as world x and z
		glm::vec2 position {};
	};

	[[nodiscard]] inline bool is_alive(const SelectedEnemy& selected) noexcept
	{
		return selected.index != 0xFFFFFFFF && selected.health > 0.0f;
	}

	[[nodiscard]] inline bool is_in_strike_range(
		const SelectedEnemy& selected,
		glm::vec2 player_position) noexcept
	{
		return glm::distance(selected.position, player_position) <= strike_range;
	}

	// The thrown sword needs a living selected enemy within its range
	[[nodiscard]] inline bool can_throw_at(
		const SelectedEnemy& selected,
		glm::vec2 player_position) noexcept
	{
		return is_alive(selected) && is_in_strike_range(selected, player_position);
	}

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

		// Puts the enemies of one of levels where they start, during the next record_update,
		// also to restart the current one
		void start_level(std::size_t level_index) noexcept;

		// Of the current level, which only simulates and draws its own
		[[nodiscard]] std::uint32_t enemy_count() const noexcept;

		// Read back a few frames late, and the whole level until the first readback after a start
		[[nodiscard]] std::uint32_t alive_count() const noexcept;

		// For the next record_update, the target and the facing are on the ground as world x and z
		// The enemies chase the target and attack it while it's alive, and the blast hits everyone
		// around it
		void prepare_update(
			glm::vec2 target,
			glm::vec2 facing,
			bool is_target_alive,
			float delta_seconds,
			float time_seconds) noexcept;

		// A world ray for a click, or the next living enemy within two tiles past aggro range
		void select_enemy(glm::vec3 origin, glm::vec3 direction) noexcept;
		void cycle_enemy() noexcept;
		[[nodiscard]] SelectedEnemy selected_enemy() const noexcept;

		// Hits during the next record_update, except the strike, which throws the sword at its
		// target and hits once land_hook says it arrived
		void cast(Ability ability) noexcept;
		void land_hook() noexcept;

		// During the next record_update, the seismic waves of Whirlwind's landing at the center,
		// in world units, the first one now at front after spreading advance more, which adds up
		// until the update so a frame without one doesn't skip anyone
		void spread_quake(glm::vec2 center, float front, float advance) noexcept;

		// During the next record_update, a damage number on every enemy without hurting anyone,
		// so a benchmark can show as many numbers as there are enemies again and again
		void cast_test_hits() noexcept;

		// Read by the vertex shaders, so they go into the renderer's descriptor sets
		// The combat state holds the target and the total damage
		[[nodiscard]] const Buffer& state_buffer() const noexcept;
		[[nodiscard]] const Buffer& combat_buffer() const noexcept;

		// Once this frame in flight's fence is signaled, reads combat feedback and the selection
		void collect(std::uint32_t frame_index) noexcept;

		// Collected since the last call, so each one is taken once, the damage the enemies dealt
		// to the player and how many enemies the abilities hit
		[[nodiscard]] std::uint32_t take_player_damage() noexcept;
		[[nodiscard]] std::uint32_t take_hit_count() noexcept;

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
		vk::raii::Pipeline selection_pipeline_;
		vk::raii::Pipeline strike_pipeline_;
		vk::raii::Pipeline tally_pipeline_;

		vk::raii::DescriptorPool descriptor_pool_ { nullptr };

		// A single set, since the buffers are the same every frame and barriers order their use
		vk::raii::DescriptorSet descriptor_set_ { nullptr };

		// Where they start in each of levels, copied in by start_level, with the slots past
		// the level's enemies dead and never drawn
		std::vector<Buffer> level_states_ {};
		std::size_t level_index_ {};

		// Updated in place every frame
		Buffer states_ {};

		// What states_ held before this frame's update, so neighbors are read before they move
		Buffer previous_states_ {};

		// The melee target, persistent selection and total damage
		Buffer combat_ {};

		// One per frame in flight, combat feedback and selected health copied for the CPU
		std::vector<Buffer> readback_buffers_ {};
		std::vector<void*> mapped_readbacks_ {};
		std::vector<bool> has_readback_ {};

		SelectedEnemy selected_enemy_ {};
		glm::vec3 selection_origin_ {};
		glm::vec3 selection_direction_ {};

		// Matches selection_mode in enemy_simulation.slang
		std::uint32_t selection_mode_ {};

		// Collected but not taken yet
		std::uint32_t player_damage_ {};
		std::uint32_t hit_count_ {};

		// Replaced by every readback
		std::uint32_t alive_count_ {};

		glm::vec2 target_ {};
		glm::vec2 facing_ { 0.0f, 1.0f };
		bool is_target_alive_ { true };
		float delta_seconds_ {};
		float time_seconds_ {};
		bool should_reset_ { false };

		// Cleared after each record_update, so every cast hits once
		bool is_blast_cast_ { false };
		bool is_strike_cast_ { false };
		bool is_test_cast_ { false };
		bool is_hook_landing_ { false };

		// The advance is cleared after each record_update too, so every wave hits an enemy once
		glm::vec2 quake_center_ {};
		float quake_front_ {};
		float quake_advance_ {};
	};
}
