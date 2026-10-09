#pragma once

namespace quokka
{
	struct DirectionalLight;

	// Between Gui::begin_frame and Gui::end_frame, and edits the light directly
	void show_light_panel(DirectionalLight& light);
}
