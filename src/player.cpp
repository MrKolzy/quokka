#include "quokka/player.hpp"

#include <cmath> // std::atan2

namespace quokka
{
	namespace
	{
		// In world units per second, faster than the enemies chase, so they can be kited
		constexpr float walk_speed { 5.0f };

		// Yaw 0 faces +z and positive values turn toward +x, so x goes first
		[[nodiscard]] float yaw_toward(glm::vec2 direction) noexcept
		{
			return std::atan2(direction.x, direction.y);
		}
	}

	// ***

	Player::Player(glm::vec2 position) noexcept
		: position_ { position }
		, yaw_ { yaw_toward(-position) }
	{
	}

	glm::vec2 Player::position() const noexcept
	{
		return position_;
	}

	float Player::yaw() const noexcept
	{
		return yaw_;
	}

	void Player::move(glm::vec2 direction, float delta_seconds) noexcept
	{
		// Standing still keeps the last facing
		if (direction == glm::vec2 { 0.0f })
		{
			return;
		}

		position_ += direction * walk_speed * delta_seconds;
		yaw_ = yaw_toward(direction);
	}
}
