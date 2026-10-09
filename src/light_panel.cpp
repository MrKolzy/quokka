#include "quokka/light_panel.hpp"

#include "quokka/light.hpp"

#include <imgui.h>

namespace quokka
{
	namespace
	{
		// See through like the stats overlay, so the scene stays visible behind it
		constexpr float background_alpha { 0.6f };

		// From the top right corner, opposite the stats overlay
		constexpr ImVec2 corner_offset { 10.0f, 10.0f };

		// Wide enough for the longest value, and the same for every slider so they line up
		constexpr float slider_width { 180.0f };
	}

	// ***

	void show_light_panel(DirectionalLight& light)
	{
		const ImGuiViewport& viewport { *ImGui::GetMainViewport() };

		// The pivot puts the window's top right corner at the position, whatever its width
		ImGui::SetNextWindowPos(
			{ viewport.WorkPos.x + viewport.WorkSize.x - corner_offset.x,
				viewport.WorkPos.y + corner_offset.y },
			ImGuiCond_FirstUseEver,
			{ 1.0f, 0.0f });
		ImGui::SetNextWindowBgAlpha(background_alpha);

		// Begin returns false while collapsed, but End must be called either way
		if (ImGui::Begin("Light", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::PushItemWidth(slider_width);

			// The full circle around, and from the horizon to straight above
			ImGui::SliderFloat("Azimuth", &light.azimuth, -180.0f, 180.0f, "%.0f deg");
			ImGui::SliderFloat("Elevation", &light.elevation, 0.0f, 90.0f, "%.0f deg");

			ImGui::ColorEdit3("Color", &light.color.x);
			ImGui::SliderFloat("Ambient", &light.ambient_strength, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Specular", &light.specular_strength, 0.0f, 1.0f, "%.2f");

			// Logarithmic, since most of the visible change happens at low values
			ImGui::SliderFloat(
				"Shininess",
				&light.shininess,
				1.0f,
				256.0f,
				"%.0f",
				ImGuiSliderFlags_Logarithmic);

			ImGui::PopItemWidth();

			if (ImGui::Button("Reset"))
			{
				light = DirectionalLight {};
			}
		}
		ImGui::End();
	}
}
