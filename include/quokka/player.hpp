#pragma once

#include <glm/glm.hpp>

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
		[[nodiscard]] bool try_blast(float time_seconds) noexcept;
		[[nodiscard]] bool try_strike(float time_seconds) noexcept;

		// For the shock wave, in seconds since the start and on the ground as world x and z
		[[nodiscard]] float last_blast_time() const noexcept;
		[[nodiscard]] glm::vec2 last_blast_point() const noexcept;

	private:
		void face_aim_point() noexcept;

		glm::vec2 position_;
		glm::vec2 aim_point_ { 0.0f };
		float yaw_ {};

		// In seconds since the start, when each ability can be cast again
		float next_blast_time_ {};
		float next_strike_time_ {};

		// Never cast yet, so there's no shock wave
		float last_blast_time_ { -std::numeric_limits<float>::infinity() };
		glm::vec2 last_blast_point_ { 0.0f };
	};
}
