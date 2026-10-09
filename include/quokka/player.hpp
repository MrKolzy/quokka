#pragma once

#include "quokka/abilities.hpp"

#include <glm/glm.hpp>

#include <array> // std::array
#include <cstddef> // std::size_t
#include <limits> // std::numeric_limits

namespace quokka
{
	class Player
	{
	public:
		// Starts aiming at the world origin, where the enemies start
		explicit Player(glm::vec2 position) noexcept;

		// On the ground, as world x and z
		[[nodiscard]] glm::vec2 position() const noexcept;

		// In radians, with the convention of EnemyState
		[[nodiscard]] float yaw() const noexcept;

		// On the ground, as world x and z, where the abilities will be cast
		[[nodiscard]] glm::vec2 aim_point() const noexcept;

		// The direction is on the ground, unit length, or zero to stand still
		// Walking keeps the player facing the aim point, so it can back away while attacking
		void move(glm::vec2 direction, float delta_seconds) noexcept;

		void aim_at(glm::vec2 point) noexcept;

		// True when the ability is ready at this time, in seconds since the start,
		// which casts it and starts its cooldown
		[[nodiscard]] bool try_cast(Ability ability, float time_seconds) noexcept;

		// In seconds, 0 once the ability is ready
		[[nodiscard]] float cooldown_left(Ability ability, float time_seconds) const noexcept;

		// For the shock wave, in seconds since the start and on the ground as world x and z
		[[nodiscard]] float last_blast_time() const noexcept;
		[[nodiscard]] glm::vec2 last_blast_point() const noexcept;

		[[nodiscard]] float health() const noexcept;
		[[nodiscard]] static float max_health() noexcept;
		[[nodiscard]] bool is_dead() const noexcept;

		// For the flash, in seconds since the start
		[[nodiscard]] float last_hit_time() const noexcept;

		// Ignored once dead, so the death can't be undone by regeneration either
		void take_damage(float damage, float time_seconds) noexcept;

		// Heals over time, once a while has passed since the last hit
		void regenerate(float delta_seconds, float time_seconds) noexcept;

	private:
		void face_aim_point() noexcept;

		glm::vec2 position_;
		glm::vec2 aim_point_ { 0.0f };
		float yaw_ {};

		// In seconds since the start, when each ability can be cast again, indexed by Ability
		std::array<float, static_cast<std::size_t>(Ability::count)> next_cast_times_ {};

		// Never cast yet, so there's no shock wave
		float last_blast_time_ { -std::numeric_limits<float>::infinity() };
		glm::vec2 last_blast_point_ { 0.0f };

		float health_ { max_health() };

		// Never hit yet, so there's no flash and regeneration can start
		float last_hit_time_ { -std::numeric_limits<float>::infinity() };
	};
}
