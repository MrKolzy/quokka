#include "quokka/player.hpp"

#include <algorithm> // std::max, std::min
#include <cmath> // std::atan2, std::cos, std::sin

namespace quokka
{
	namespace
	{
		// In world units per second, faster than the enemies chase, so they can be kited
		constexpr float walk_speed { 7.0f };

		// The run clip's steps match a speed of 5, so it plays faster with a quicker player
		// and the feet don't slide
		constexpr float run_animation_speed { walk_speed / 5.0f };

		// The enemies' attacks from enemy_attack_damage in level.hpp are balanced around it, so on
		// normal the ring of about 15 that fits around the player kills it in about 15 seconds
		constexpr float starting_health { 1000.0f };

		// Per second, starting this many seconds after the last hit, so it helps between fights
		// but not while surrounded
		constexpr float regeneration_rate { 20.0f };
		constexpr float regeneration_delay { 3.0f };

		// The cast plays the attack clip, a throw, faster so it fits between quick strikes
		// The clip is 1 second long, so it lasts 1 / speed seconds
		constexpr float cast_animation_speed { 3.0f };
		constexpr float cast_animation_duration { 1.0f / cast_animation_speed };

		// In seconds after Whirlwind's landing, how long the landing crouch plays, which is most
		// of its clip
		constexpr float landing_animation_duration { 0.6f };

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

	glm::vec2 Player::facing() const noexcept
	{
		// The inverse of yaw_toward
		return { std::sin(yaw_), std::cos(yaw_) };
	}

	void Player::move(glm::vec2 direction, float delta_seconds) noexcept
	{
		position_ += direction * walk_speed * delta_seconds;
		is_moving_ = direction != glm::vec2 { 0.0f };

		if (is_moving_)
		{
			yaw_ = yaw_toward(direction);
		}
	}

	bool Player::try_cast(Ability ability, float time_seconds) noexcept
	{
		float& next_cast_time { next_cast_times_[static_cast<std::size_t>(ability)] };
		if (time_seconds < next_cast_time)
		{
			return false;
		}

		next_cast_time = time_seconds + ability_info(ability).cooldown;
		last_cast_time_ = time_seconds;

		if (ability == Ability::strike)
		{
			last_strike_time_ = time_seconds;
		}
		else if (ability == Ability::blast)
		{
			last_blast_time_ = time_seconds;
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

	float Player::last_strike_time() const noexcept
	{
		return last_strike_time_;
	}

	void Player::mark_strike_range(float time_seconds) noexcept
	{
		strike_range_time_ = time_seconds;
	}

	float Player::strike_range_time() const noexcept
	{
		return strike_range_time_;
	}

	void Player::slam() noexcept
	{
		quake_point_ = position_;
	}

	glm::vec2 Player::quake_point() const noexcept
	{
		return quake_point_;
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

		if (is_dead())
		{
			death_time_ = time_seconds;
		}
	}

	void Player::regenerate(float delta_seconds, float time_seconds) noexcept
	{
		if (is_dead() || time_seconds - last_hit_time_ < regeneration_delay)
		{
			return;
		}

		health_ = std::min(health_ + regeneration_rate * delta_seconds, max_health());
	}

	AnimationState Player::animation(float time_seconds) const noexcept
	{
		if (is_dead())
		{
			return { AnimationClip::death, time_seconds - death_time_ };
		}

		// Whirlwind's leap after its last turn and the landing, before any other cast
		const float time_since_landing { time_seconds - last_blast_time_ - whirlwind_land_time };
		if (time_since_landing >= -whirlwind_jump_duration && time_since_landing < 0.0f)
		{
			return { AnimationClip::jump, time_since_landing + whirlwind_jump_duration };
		}

		if (time_since_landing >= 0.0f && time_since_landing < landing_animation_duration)
		{
			return { AnimationClip::land, time_since_landing };
		}

		const float time_since_cast { time_seconds - last_cast_time_ };
		if (time_since_cast < cast_animation_duration)
		{
			return { AnimationClip::attack, time_since_cast * cast_animation_speed };
		}

		if (is_moving_)
		{
			return { AnimationClip::run, time_seconds * run_animation_speed };
		}

		return { AnimationClip::idle, time_seconds };
	}
}
