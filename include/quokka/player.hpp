#pragma once

#include <glm/glm.hpp>

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

	private:
		void face_aim_point() noexcept;

		glm::vec2 position_;
		glm::vec2 aim_point_ { 0.0f };
		float yaw_ {};
	};
}
