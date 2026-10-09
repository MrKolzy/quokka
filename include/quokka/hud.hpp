#pragma once

namespace quokka
{
	class Player;

	// Between Gui::begin_frame and Gui::end_frame, the game's own interface, always on screen
	// At the bottom center the player's health over the keys of hotbar_slots, with each
	// ability's cooldown and a tooltip describing it, and a death screen once the player dies
	void show_hud(const Player& player, float time_seconds);
}
