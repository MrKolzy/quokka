#pragma once

#include "quokka/abilities.hpp"
#include "quokka/animation.hpp"
#include "quokka/constants.hpp"

#include <glm/glm.hpp>

#include <array> // std::array
#include <cstddef> // std::size_t

namespace quokka
{
	struct AnimationState
	{
		AnimationClip clip;

		// In seconds into the clip
		float time;
	};

	class Player
	{
	public:
		// Starts facing the world origin, where the enemies start
		explicit Player(glm::vec2 position) noexcept;

		// On the ground, as world x and z
		[[nodiscard]] glm::vec2 position() const noexcept;

		// In radians, with the convention of EnemyState
		[[nodiscard]] float yaw() const noexcept;

		// Unit length on the ground, as world x and z
		[[nodiscard]] glm::vec2 facing() const noexcept;

		// The direction is on the ground, unit length, or zero to stand still
		// Walking turns the player to face where it goes, and standing still keeps the facing
		void move(glm::vec2 direction, float delta_seconds) noexcept;

		// True when the ability is ready at this time, in seconds since the start,
		// which casts it and starts its cooldown
		[[nodiscard]] bool try_cast(Ability ability, float time_seconds) noexcept;

		// In seconds, 0 once the ability is ready
		[[nodiscard]] float cooldown_left(Ability ability, float time_seconds) const noexcept;

		// For Whirlwind's spin and shock wave, in seconds since the start
		[[nodiscard]] float last_blast_time() const noexcept;

		// For when the thrown sword lands, in seconds since the start
		[[nodiscard]] float last_strike_time() const noexcept;

		// Trying to throw the sword without a target in reach marks its range on the ground for
		// a moment, and holding the key keeps it there, with the time in seconds since the start
		void mark_strike_range(float time_seconds) noexcept;
		[[nodiscard]] float strike_range_time() const noexcept;

		// Whirlwind's landing, which marks where its quake spreads from as world x and z
		void slam() noexcept;
		[[nodiscard]] glm::vec2 quake_point() const noexcept;

		[[nodiscard]] float health() const noexcept;
		[[nodiscard]] static float max_health() noexcept;
		[[nodiscard]] bool is_dead() const noexcept;

		// For the flash, in seconds since the start
		[[nodiscard]] float last_hit_time() const noexcept;

		// Ignored once dead, so the death can't be undone by regeneration either
		void take_damage(float damage, float time_seconds) noexcept;

		// Heals over time, once a while has passed since the last hit
		void regenerate(float delta_seconds, float time_seconds) noexcept;

		// Dying, then casting, then running, then standing, each timed from when it started,
		// with the time in seconds
		[[nodiscard]] AnimationState animation(float time_seconds) const noexcept;

	private:
		glm::vec2 position_;
		float yaw_ {};

		// In seconds since the start, when each ability can be cast again, indexed by Ability
		std::array<float, static_cast<std::size_t>(Ability::count)> next_cast_times_ {};

		// Never cast yet, so there's no shock wave
		float last_blast_time_ { constants::long_ago };
		float last_strike_time_ { constants::long_ago };
		float strike_range_time_ { constants::long_ago };
		glm::vec2 quake_point_ { 0.0f };

		float health_ { max_health() };

		// Never hit yet, so there's no flash and regeneration can start
		float last_hit_time_ { constants::long_ago };

		// For the animation, in seconds since the start
		float last_cast_time_ { constants::long_ago };
		float death_time_ {};

		// Whether the last move had a direction
		bool is_moving_ { false };
	};
}
