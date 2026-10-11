#pragma once

#include "quokka/window.hpp"

#include <array> // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint32_t
#include <optional> // std::optional

namespace quokka
{
	enum class Ability
	{
		strike,
		blast,
		count
	};

	struct AbilityInfo
	{
		const char* name;
		const char* description;

		// Rolled evenly between both for every enemy hit, before a critical hit doubles it
		float min_damage;
		float max_damage;

		// In seconds between casts, also while the key is held
		float cooldown;
	};

	// The chance in percent of any hit being critical, which deals double damage
	inline constexpr std::uint32_t critical_chance { 15 };

	// In world units around the player, enough for the ring of enemies attacking it and the one
	// pressing behind them, must match blast_radius in enemy.slang
	inline constexpr float blast_radius { 4.5f };

	// In world units from the player, how far away the sword can be thrown, as far as Tab selects,
	// must match strike_range in enemy.slang, where the ground marks it
	inline constexpr float strike_range { 12.0f };

	// The thrown sword flies for this long in seconds, then its black hole pulls in every enemy
	// within the radius in world units, must match hook_flight_duration and hook_radius
	// in enemy.slang, which also times the rest of the pull
	inline constexpr float hook_flight_duration { 0.2f };
	inline constexpr float hook_radius { 8.0f };

	// Whirlwind turns this many times, each one the other way, and each turn takes this long in
	// seconds, must match whirlwind_turns and whirlwind_turn_duration in shader.slang
	inline constexpr std::uint32_t whirlwind_turns { 6 };
	inline constexpr float whirlwind_turn_duration { 0.13f };

	// After the last turn the player leaps for this long in seconds and lands driving the sword
	// into the ground, must match whirlwind_jump_duration in shader.slang
	inline constexpr float whirlwind_jump_duration { 0.45f };
	inline constexpr float whirlwind_land_time {
		static_cast<float>(whirlwind_turns) * whirlwind_turn_duration + whirlwind_jump_duration
	};

	// The landing sends this many seismic waves out from where it hits, each this far in world
	// units behind the last, spreading this fast in world units per second up to this radius,
	// must match the quake constants in enemy.slang
	inline constexpr std::uint32_t quake_wave_count { 3 };
	inline constexpr float quake_wave_spacing { 3.0f };
	inline constexpr float quake_wave_speed { 24.0f };
	inline constexpr float quake_radius { 12.0f };

	// How far the first wave spreads, so the last one reaches the radius
	inline constexpr float quake_last_front {
		quake_radius + static_cast<float>(quake_wave_count - 1) * quake_wave_spacing
	};

	// Rolled like an ability's damage for every wave that reaches an enemy
	inline constexpr float quake_min_damage { 200.0f };
	inline constexpr float quake_max_damage { 400.0f };

	inline constexpr std::array<AbilityInfo, static_cast<std::size_t>(Ability::count)> abilities {
		AbilityInfo {
			.name { "Void Sword" },
			.description { "Hurls your sword at your target, pulling nearby enemies back to you." },
			.min_damage { 300.0f },
			.max_damage { 500.0f },
			// The pull gathers a whole pack, which sets up Whirlwind, so it comes back less often
			.cooldown { 8.0f }
		},
		AbilityInfo {
			.name { "Whirlwind" },
			.description { "Spins six times, then leaps into a slam that sends out quakes." },
			.min_damage { 150.0f },
			.max_damage { 300.0f },
			// Six hits and three quakes kill a pack on their own, so it can't be cast nonstop
			.cooldown { 6.0f }
		}
	};

	[[nodiscard]] constexpr const AbilityInfo& ability_info(Ability ability) noexcept
	{
		return abilities[static_cast<std::size_t>(ability)];
	}

	// A key of the bar at the bottom of the screen and the ability it casts, if any
	struct HotbarSlot
	{
		Key key;
		const char* key_label;
		std::optional<Ability> ability;
	};

	// Only the number keys, since the letters around WASD already walk, reset and fly
	inline constexpr std::array hotbar_slots {
		HotbarSlot { Key::one, "1", Ability::strike },
		HotbarSlot { Key::two, "2", Ability::blast },
		HotbarSlot { Key::three, "3", std::nullopt },
		HotbarSlot { Key::four, "4", std::nullopt },
		HotbarSlot { Key::five, "5", std::nullopt }
	};
}
