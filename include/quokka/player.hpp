#pragma once

#include <glm/glm.hpp>

namespace quokka
{
	class Player
	{
	public:
		// Starts facing the world origin, where the enemies start
		explicit Player(glm::vec2 position) noexcept;

		// On the ground, as world x and z
		[[nodiscard]] glm::vec2 position() const noexcept;

		// In radians, with the convention of EnemyState
		[[nodiscard]] float yaw() const noexcept;

		// The direction is on the ground, unit length, or zero to stand still
		// Walking turns the player to face where it goes
		void move(glm::vec2 direction, float delta_seconds) noexcept;

	private:
		glm::vec2 position_;
		float yaw_;
	};
}
