#pragma once

namespace quokka
{
	class Player;

	// Between Gui::begin_frame and Gui::end_frame, the keys of hotbar_slots at the bottom center
	// of the screen, with each ability's cooldown and a tooltip describing it
	void show_hotbar(const Player& player, float time_seconds);
}
