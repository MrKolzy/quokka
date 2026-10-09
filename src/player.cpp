#include "quokka/player.hpp"

#include <algorithm> // std::max, std::min
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

		// The enemies' attacks in enemy_simulation.slang are balanced around it, so the ring
		// of about 15 that fits around the player kills it in about 15 seconds
		constexpr float starting_health { 1000.0f };

		// Per second, starting this many seconds after the last hit, so it helps between fights
		// but not while surrounded
		constexpr float regeneration_rate { 20.0f };
		constexpr float regeneration_delay { 3.0f };

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

	bool Player::try_cast(Ability ability, float time_seconds) noexcept
	{
		float& next_cast_time { next_cast_times_[static_cast<std::size_t>(ability)] };
		if (time_seconds < next_cast_time)
		{
			return false;
		}

		next_cast_time = time_seconds + ability_info(ability).cooldown;

		if (ability == Ability::blast)
		{
			last_blast_time_ = time_seconds;
			last_blast_point_ = aim_point_;
		}

		return true;
	}

	float Player::cooldown_left(Ability ability, float time_seconds) const noexcept
	{
		return std::max(next_cast_times_[static_cast<std::size_t>(ability)] - time_seconds, 0.0f);
	}

	float Player::last_blast_time() const noexcept
	{
		return last_blast_time_;
	}

	glm::vec2 Player::last_blast_point() const noexcept
	{
		return last_blast_point_;
	}

	float Player::health() const noexcept
	{
		return health_;
	}

	float Player::max_health() noexcept
	{
		return starting_health;
	}

	bool Player::is_dead() const noexcept
	{
		return health_ <= 0.0f;
	}

	float Player::last_hit_time() const noexcept
	{
		return last_hit_time_;
	}

	void Player::take_damage(float damage, float time_seconds) noexcept
	{
		if (is_dead() || damage <= 0.0f)
		{
			return;
		}

		health_ = std::max(health_ - damage, 0.0f);
		last_hit_time_ = time_seconds;
	}

	void Player::regenerate(float delta_seconds, float time_seconds) noexcept
	{
		if (is_dead() || time_seconds - last_hit_time_ < regeneration_delay)
		{
			return;
		}

		health_ = std::min(health_ + regeneration_rate * delta_seconds, max_health());
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
