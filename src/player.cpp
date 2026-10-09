#include "quokka/player.hpp"

#include <cmath> // std::atan2

namespace quokka
{
	namespace
	{
		// In world units per second, faster than the enemies chase, so they can be kited
		constexpr float walk_speed { 5.0f };

		// In world units, closer than this the direction to the aim point is too unsteady to face,
		// for example with the cursor right below the player
		constexpr float min_aim_distance { 0.3f };

		// Yaw 0 faces +z and positive values turn toward +x, so x goes first
		[[nodiscard]] float yaw_toward(glm::vec2 direction) noexcept
		{
			return std::atan2(direction.x, direction.y);
		}
	}

	// ***

	Player::Player(glm::vec2 position) noexcept
		: position_ { position }
	{
		face_aim_point();
	}

	glm::vec2 Player::position() const noexcept
	{
		return position_;
	}

	float Player::yaw() const noexcept
	{
		return yaw_;
	}

	glm::vec2 Player::aim_point() const noexcept
	{
		return aim_point_;
	}

	void Player::move(glm::vec2 direction, float delta_seconds) noexcept
	{
		position_ += direction * walk_speed * delta_seconds;
		face_aim_point();
	}

	void Player::aim_at(glm::vec2 point) noexcept
	{
		aim_point_ = point;
		face_aim_point();
	}

	void Player::face_aim_point() noexcept
	{
		// Too close keeps the last facing
		const glm::vec2 to_aim_point { aim_point_ - position_ };
		if (glm::length(to_aim_point) >= min_aim_distance)
		{
			yaw_ = yaw_toward(to_aim_point);
		}
	}
}
