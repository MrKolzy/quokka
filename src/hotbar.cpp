#include "quokka/hotbar.hpp"

#include "quokka/abilities.hpp"
#include "quokka/gui.hpp"
#include "quokka/player.hpp"

#include <imgui.h>

#include <cmath> // std::cos, std::sin
#include <cstddef> // std::size_t
#include <format> // std::format
#include <numbers> // std::numbers::pi_v
#include <string> // std::string

namespace quokka
{
	namespace
	{
		// In pixels, big enough to read the cooldown left inside a slot
		constexpr float slot_size { 52.0f };
		constexpr float slot_spacing { 6.0f };
		constexpr float slot_rounding { 6.0f };

		// From the bottom edge, like the action bars of most games
		constexpr float bottom_margin { 12.0f };

		// See through like the debug panels, so the scene stays visible behind it
		constexpr float background_alpha { 0.6f };

		// Wraps descriptions at this many characters of the font's size
		constexpr float tooltip_wrap_width { 18.0f };

		// Enough triangles for the cooldown sweep to look round at this size
		constexpr int sweep_segments { 48 };

		// In sRGB, as image editors show colors, converted for the sRGB swapchain
		[[nodiscard]] ImVec4 srgb_color(float red, float green, float blue, float alpha = 1.0f)
		{
			return { srgb_to_linear(red), srgb_to_linear(green), srgb_to_linear(blue), alpha };
		}

		[[nodiscard]] ImU32 to_u32(const ImVec4& color)
		{
			return ImGui::ColorConvertFloat4ToU32(color);
		}

		// The blast matches the aim ring on the ground, which shows its area
		[[nodiscard]] ImVec4 ability_color(Ability ability)
		{
			return ability == Ability::blast
				? srgb_color(1.0f, 0.76f, 0.38f)
				: srgb_color(0.6f, 0.8f, 1.0f);
		}

		// Simple shapes instead of textures, centered in the slot
		void draw_icon(ImDrawList& draw_list, Ability ability, ImVec2 center)
		{
			const ImU32 color { to_u32(ability_color(ability)) };
			const float unit { slot_size / 2.0f };

			if (ability == Ability::blast)
			{
				// A burst, a core with rays all around
				draw_list.AddCircleFilled(center, 0.22f * unit, color);

				constexpr int ray_count { 8 };
				for (int ray { 0 }; ray < ray_count; ++ray)
				{
					const float angle {
						2.0f * std::numbers::pi_v<float> * static_cast<float>(ray) /
						static_cast<float>(ray_count)
					};
					const ImVec2 direction { std::cos(angle) * unit, std::sin(angle) * unit };

					draw_list.AddLine(
						{ center.x + direction.x * 0.35f, center.y + direction.y * 0.35f },
						{ center.x + direction.x * 0.6f, center.y + direction.y * 0.6f },
						color,
						3.0f);
				}

				return;
			}

			// A sword pointing up and to the right, with its crossguard across the blade
			const ImVec2 tip { center.x + 0.5f * unit, center.y - 0.5f * unit };
			const ImVec2 guard { center.x - 0.25f * unit, center.y + 0.25f * unit };
			const ImVec2 pommel { center.x - 0.5f * unit, center.y + 0.5f * unit };

			draw_list.AddLine(guard, tip, color, 4.0f);
			draw_list.AddLine(guard, pommel, color, 3.0f);
			draw_list.AddLine(
				{ guard.x - 0.2f * unit, guard.y - 0.2f * unit },
				{ guard.x + 0.2f * unit, guard.y + 0.2f * unit },
				color,
				3.0f);
		}

		// Darkens the part of the slot still cooling down, which shrinks clockwise from the top
		void draw_cooldown(ImDrawList& draw_list, ImVec2 min, ImVec2 max, float fraction_left)
		{
			const ImVec2 center { (min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f };

			// Past the corners, so the clipped fan covers the whole square
			const float radius { slot_size };

			// From the angle the sweep has reached to the top, where it ends
			constexpr float top { -std::numbers::pi_v<float> / 2.0f };
			const float start { top + 2.0f * std::numbers::pi_v<float> * (1.0f - fraction_left) };
			const float end { top + 2.0f * std::numbers::pi_v<float> };
			const float segment_angle { (end - start) / static_cast<float>(sweep_segments) };

			const ImU32 shade { to_u32({ 0.0f, 0.0f, 0.0f, 0.7f }) };

			draw_list.PushClipRect(min, max, true);
			for (int segment { 0 }; segment < sweep_segments; ++segment)
			{
				const float from { start + segment_angle * static_cast<float>(segment) };
				const float to { from + segment_angle };

				draw_list.AddTriangleFilled(
					center,
					{ center.x + std::cos(from) * radius, center.y + std::sin(from) * radius },
					{ center.x + std::cos(to) * radius, center.y + std::sin(to) * radius },
					shade);
			}
			draw_list.PopClipRect();
		}

		void show_tooltip(Ability ability, const HotbarSlot& slot)
		{
			const AbilityInfo& info { ability_info(ability) };
			const char* const mouse_button {
				info.mouse_button == MouseButton::left ? "left" : "right"
			};

			ImGui::TextColored(ability_color(ability), "%s", info.name);
			ImGui::TextDisabled("%s", std::format(
				"Key {} or {} click, hold to repeat", slot.key_label, mouse_button).c_str());
			ImGui::Separator();

			ImGui::PushTextWrapPos(ImGui::GetFontSize() * tooltip_wrap_width);
			ImGui::TextUnformatted(info.description);
			ImGui::PopTextWrapPos();

			ImGui::TextUnformatted(
				std::format("Damage: {:.0f} to {:.0f}", info.min_damage, info.max_damage).c_str());
			ImGui::TextUnformatted(std::format("Cooldown: {:.1f} s", info.cooldown).c_str());

			if (ability == Ability::blast)
			{
				ImGui::TextUnformatted(std::format("Radius: {:.0f} m", blast_radius).c_str());
			}

			ImGui::TextDisabled("%s", std::format(
				"{}% chance of a critical hit for double damage", critical_chance).c_str());
		}

		void show_slot(const HotbarSlot& slot, const Player& player, float time_seconds)
		{
			// Takes the slot's place in the layout and reports hovering, without drawing
			ImGui::InvisibleButton(slot.key_label, { slot_size, slot_size });

			const ImVec2 min { ImGui::GetItemRectMin() };
			const ImVec2 max { ImGui::GetItemRectMax() };
			const ImVec2 center { (min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f };
			ImDrawList& draw_list { *ImGui::GetWindowDrawList() };

			const ImU32 background_color { to_u32(srgb_color(0.08f, 0.08f, 0.1f)) };
			draw_list.AddRectFilled(min, max, background_color, slot_rounding);

			if (slot.ability)
			{
				const Ability ability { *slot.ability };
				draw_icon(draw_list, ability, center);

				const float cooldown_left { player.cooldown_left(ability, time_seconds) };
				if (cooldown_left > 0.0f)
				{
					const float fraction_left { cooldown_left / ability_info(ability).cooldown };
					draw_cooldown(draw_list, min, max, fraction_left);

					const std::string seconds { std::format("{:.1f}", cooldown_left) };
					const ImVec2 text_size { ImGui::CalcTextSize(seconds.c_str()) };
					const ImVec2 text_position {
						center.x - text_size.x / 2.0f, center.y - text_size.y / 2.0f
					};

					// A shadow in every diagonal, so it reads over the icon
					for (const ImVec2 shift : { ImVec2 { -1.0f, -1.0f }, ImVec2 { 1.0f, -1.0f },
						ImVec2 { -1.0f, 1.0f }, ImVec2 { 1.0f, 1.0f } })
					{
						draw_list.AddText(
							{ text_position.x + shift.x, text_position.y + shift.y },
							to_u32({ 0.0f, 0.0f, 0.0f, 1.0f }),
							seconds.c_str());
					}

					const ImU32 text_color { to_u32({ 1.0f, 1.0f, 1.0f, 1.0f }) };
					draw_list.AddText(text_position, text_color, seconds.c_str());
				}

				if (ImGui::BeginItemTooltip())
				{
					show_tooltip(ability, slot);
					ImGui::EndTooltip();
				}
			}

			// Brighter around the slots that hold an ability
			const ImVec4 border_color {
				slot.ability ? srgb_color(0.55f, 0.55f, 0.6f) : srgb_color(0.3f, 0.3f, 0.33f)
			};
			draw_list.AddRect(min, max, to_u32(border_color), slot_rounding, 0, 1.5f);

			draw_list.AddText(
				{ min.x + 4.0f, min.y + 2.0f },
				to_u32(srgb_color(0.9f, 0.9f, 0.9f)),
				slot.key_label);
		}
	}

	// ***

	void show_hotbar(const Player& player, float time_seconds)
	{
		const ImGuiViewport& viewport { *ImGui::GetMainViewport() };

		// Every frame, so it stays centered when the window is resized
		// The pivot puts the window's bottom center at the position, whatever its size
		ImGui::SetNextWindowPos(
			{ viewport.WorkPos.x + viewport.WorkSize.x / 2.0f,
				viewport.WorkPos.y + viewport.WorkSize.y - bottom_margin },
			ImGuiCond_Always,
			{ 0.5f, 1.0f });
		ImGui::SetNextWindowBgAlpha(background_alpha);

		// Part of the game, so it can't be moved or focused like the debug panels
		constexpr ImGuiWindowFlags window_flags {
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
		};

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { slot_spacing, slot_spacing });

		if (ImGui::Begin("Hotbar", nullptr, window_flags))
		{
			for (std::size_t index { 0 }; index < hotbar_slots.size(); ++index)
			{
				// The number row first, then the letters below it
				if (index % hotbar_row_length != 0)
				{
					ImGui::SameLine();
				}

				show_slot(hotbar_slots[index], player, time_seconds);
			}
		}
		ImGui::End();

		ImGui::PopStyleVar();
	}
}
