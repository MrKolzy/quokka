#pragma once

#include <cstdint> // std::uint32_t
#include <optional> // std::optional

namespace quokka
{
	class Player;
	struct Level;
	struct SelectedEnemy;

	struct LevelProgress
	{
		std::uint32_t enemies_left;

		// In seconds, how long it took to kill every enemy, empty until then
		std::optional<float> clear_seconds;
	};

	// Between Gui::begin_frame and Gui::end_frame, the game's own interface, always on screen
	// At the bottom center the level and its enemies left over the player's health and the keys
	// of hotbar_slots, with each ability's cooldown and a tooltip describing it, and a screen
	// once the player dies or clears the level
	// The selected enemy's name and health stay at the top center
	void show_hud(
		const Player& player,
		float time_seconds,
		const SelectedEnemy& selected,
		const Level& level,
		const LevelProgress& progress);
}
