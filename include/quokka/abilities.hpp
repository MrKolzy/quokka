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

		// Also cast with this mouse button, like the main and secondary skills of Diablo
		MouseButton mouse_button;

		// Rolled evenly between both for every enemy hit, before a critical hit doubles it
		float min_damage;
		float max_damage;

		// In seconds between casts, also while the key or button is held
		float cooldown;
	};

	// The chance in percent of any hit being critical, which deals double damage
	inline constexpr std::uint32_t critical_chance { 15 };

	// In world units, must match blast_radius in enemy.slang
	inline constexpr float blast_radius { 3.0f };

	inline constexpr std::array<AbilityInfo, static_cast<std::size_t>(Ability::count)> abilities {
		AbilityInfo {
			.name { "Strike" },
			.description { "Hits the enemy closest to the cursor, which glows brighter." },
			.mouse_button { MouseButton::left },
			.min_damage { 300.0f },
			.max_damage { 500.0f },
			.cooldown { 0.3f }
		},
		AbilityInfo {
			.name { "Blast" },
			.description { "Explodes at the cursor, hitting every enemy inside the circle." },
			.mouse_button { MouseButton::right },
			.min_damage { 150.0f },
			.max_damage { 300.0f },
			.cooldown { 0.8f }
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

	// The number row above the letters, like on the keyboard
	inline constexpr std::size_t hotbar_row_length { 5 };

	inline constexpr std::array<HotbarSlot, 2 * hotbar_row_length> hotbar_slots {
		HotbarSlot { Key::one, "1", Ability::strike },
		HotbarSlot { Key::two, "2", Ability::blast },
		HotbarSlot { Key::three, "3", std::nullopt },
		HotbarSlot { Key::four, "4", std::nullopt },
		HotbarSlot { Key::five, "5", std::nullopt },
		HotbarSlot { Key::q, "Q", std::nullopt },
		HotbarSlot { Key::w, "W", std::nullopt },
		HotbarSlot { Key::e, "E", std::nullopt },
		HotbarSlot { Key::r, "R", std::nullopt },
		HotbarSlot { Key::t, "T", std::nullopt }
	};
}
