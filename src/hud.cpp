#include "quokka/hud.hpp"

#include "quokka/abilities.hpp"
#include "quokka/enemy_simulation.hpp"
#include "quokka/gui.hpp"
#include "quokka/level.hpp"
#include "quokka/player.hpp"

#include <imgui.h>

#include <algorithm> // std::clamp, std::min
#include <cfloat> // FLT_MAX
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

		// In pixels, as wide as the slots below it
		constexpr float health_bar_height { 18.0f };

		// In pixels, the title of the death and clear screens and the hints below it
		constexpr float end_title_size { 64.0f };
		constexpr float end_hint_size { 22.0f };

		// In sRGB, as image editors show colors, converted for the sRGB swapchain
		[[nodiscard]] ImVec4 srgb_color(float red, float green, float blue, float alpha = 1.0f)
		{
			return { srgb_to_linear(red), srgb_to_linear(green), srgb_to_linear(blue), alpha };
		}

		[[nodiscard]] ImU32 to_u32(const ImVec4& color)
		{
			return ImGui::ColorConvertFloat4ToU32(color);
		}

		// White with a black shadow in every diagonal, so it reads over anything behind it
		void add_centered_text(
			ImDrawList& draw_list,
			float font_size,
			ImVec2 center,
			const char* text,
			ImU32 color = IM_COL32_WHITE)
		{
			ImFont& font { *ImGui::GetFont() };
			const ImVec2 size { font.CalcTextSizeA(font_size, FLT_MAX, 0.0f, text) };
			const ImVec2 position { center.x - size.x / 2.0f, center.y - size.y / 2.0f };

			for (const ImVec2 shift : { ImVec2 { -1.0f, -1.0f }, ImVec2 { 1.0f, -1.0f },
				ImVec2 { -1.0f, 1.0f }, ImVec2 { 1.0f, 1.0f } })
			{
				draw_list.AddText(
					&font,
					font_size,
					{ position.x + shift.x, position.y + shift.y },
					IM_COL32_BLACK,
					text);
			}

			draw_list.AddText(&font, font_size, position, color, text);
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

			ImGui::TextColored(ability_color(ability), "%s", info.name);
			const std::string key_hint { std::format("Key {}, hold to repeat", slot.key_label) };
			ImGui::TextDisabled("%s", key_hint.c_str());
			ImGui::Separator();

			ImGui::PushTextWrapPos(ImGui::GetFontSize() * tooltip_wrap_width);
			ImGui::TextUnformatted(info.description);
			ImGui::PopTextWrapPos();

			ImGui::TextUnformatted(
				std::format("Damage: {:.0f} to {:.0f}", info.min_damage, info.max_damage).c_str());
			ImGui::TextUnformatted(std::format("Cooldown: {:.1f} s", info.cooldown).c_str());

			if (ability == Ability::strike)
			{
				ImGui::TextUnformatted(std::format("Range: {:.0f} m", strike_range).c_str());
				ImGui::TextUnformatted(std::format("Pull radius: {:.0f} m", hook_radius).c_str());
				ImGui::TextDisabled("Needs a selected enemy in range");
			}
			else if (ability == Ability::blast)
			{
				ImGui::TextUnformatted(std::format("Radius: {:.1f} m", blast_radius).c_str());
			}

			ImGui::TextDisabled("%s", std::format(
				"{}% chance of a critical hit for double damage", critical_chance).c_str());
		}

		void show_slot(
			const HotbarSlot& slot,
			const Player& player,
			float time_seconds,
			const SelectedEnemy& selected)
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

				// Dimmed while there's nothing in reach to throw the sword at
				if (ability == Ability::strike && !can_throw_at(selected, player.position()))
				{
					const ImU32 dim_color { to_u32(srgb_color(0.0f, 0.0f, 0.0f, 0.6f)) };
					draw_list.AddRectFilled(min, max, dim_color, slot_rounding);
				}

				const float cooldown_left { player.cooldown_left(ability, time_seconds) };
				if (cooldown_left > 0.0f)
				{
					const float fraction_left { cooldown_left / ability_info(ability).cooldown };
					draw_cooldown(draw_list, min, max, fraction_left);

					const std::string seconds { std::format("{:.1f}", cooldown_left) };
					add_centered_text(draw_list, ImGui::GetFontSize(), center, seconds.c_str());
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

		// In pixels, the row of slots, which the lines above it span too
		[[nodiscard]] float hotbar_width()
		{
			const auto slot_count { static_cast<float>(hotbar_slots.size()) };

			return slot_size * slot_count + slot_spacing * (slot_count - 1.0f);
		}

		// The level and its difficulty on the left, the enemies still alive on the right
		void show_level_line(const Level& level, const LevelProgress& progress)
		{
			const std::string name {
				std::format("{}, {}", level.name, difficulty_name(level.difficulty))
			};
			const std::string enemies {
				std::format("Enemies {} / {}", progress.enemies_left, level_enemy_count(level))
			};

			ImGui::TextUnformatted(name.c_str());
			ImGui::SameLine();
			ImGui::SetCursorPosX(
				ImGui::GetCursorStartPos().x + hotbar_width() -
				ImGui::CalcTextSize(enemies.c_str()).x);
			ImGui::TextUnformatted(enemies.c_str());
		}

		// As wide as the row of slots, red like the enemies' bars, with the exact health on it
		void show_health_bar(const Player& player)
		{
			// Takes the bar's place in the layout, without drawing
			ImGui::Dummy({ hotbar_width(), health_bar_height });

			const ImVec2 min { ImGui::GetItemRectMin() };
			const ImVec2 max { ImGui::GetItemRectMax() };
			ImDrawList& draw_list { *ImGui::GetWindowDrawList() };

			const float fraction { player.health() / Player::max_health() };
			const ImVec2 fill_max { min.x + (max.x - min.x) * fraction, max.y };

			draw_list.AddRectFilled(min, max, to_u32(srgb_color(0.15f, 0.02f, 0.02f)), 4.0f);
			draw_list.AddRectFilled(min, fill_max, to_u32(srgb_color(0.8f, 0.1f, 0.08f)), 4.0f);
			draw_list.AddRect(min, max, to_u32(srgb_color(0.55f, 0.55f, 0.6f)), 4.0f, 0, 1.5f);

			const std::string text {
				std::format("{:.0f} / {:.0f}", player.health(), Player::max_health())
			};
			add_centered_text(
				draw_list,
				ImGui::GetFontSize(),
				{ (min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f },
				text.c_str());
		}

		void show_selected_enemy(const SelectedEnemy& selected, glm::vec2 player_position)
		{
			if (!is_alive(selected))
			{
				return;
			}

			const ImGuiViewport& viewport { *ImGui::GetMainViewport() };
			ImGui::SetNextWindowPos(
				{ viewport.WorkPos.x + viewport.WorkSize.x / 2.0f, viewport.WorkPos.y + 18.0f },
				ImGuiCond_Always,
				{ 0.5f, 0.0f });
			ImGui::SetNextWindowBgAlpha(background_alpha);

			constexpr ImGuiWindowFlags flags {
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
				ImGuiWindowFlags_NoInputs
			};
			if (ImGui::Begin("Selected enemy", nullptr, flags))
			{
				const float width { std::min(280.0f, viewport.WorkSize.x * 0.7f) };
				const std::string name { std::format("Skeleton Minion #{}", selected.index + 1) };
				const float text_width { ImGui::CalcTextSize(name.c_str()).x };
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - text_width) / 2.0f);
				ImGui::TextUnformatted(name.c_str());

				// Must match enemy_max_health in enemy.slang
				constexpr float max_health { 1000.0f };
				const float health { std::clamp(selected.health, 0.0f, max_health) };
				const std::string label { std::format("{:.0f} / {:.0f}", health, max_health) };
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, srgb_color(0.8f, 0.1f, 0.08f));
				ImGui::ProgressBar(health / max_health, { width, 22.0f }, label.c_str());
				ImGui::PopStyleColor();

				// Centered under the bar, so it's clear why the sword can't be thrown
				if (!is_in_strike_range(selected, player_position))
				{
					constexpr const char* out_of_range { "Out of range" };
					const float warning_width { ImGui::CalcTextSize(out_of_range).x };
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - warning_width) / 2.0f);
					ImGui::TextColored(srgb_color(1.0f, 0.35f, 0.3f), "%s", out_of_range);
				}
			}
			ImGui::End();
		}

		// Darkens the whole screen behind the title and the hints below it, over every window
		void show_end_screen(
			const char* title,
			const ImVec4& title_color,
			const char* hint,
			const char* second_hint = nullptr)
		{
			const ImGuiViewport& viewport { *ImGui::GetMainViewport() };
			const ImVec2 min { viewport.Pos };
			const ImVec2 max { viewport.Pos.x + viewport.Size.x, viewport.Pos.y + viewport.Size.y };
			const ImVec2 center { (min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f };

			ImDrawList& draw_list { *ImGui::GetForegroundDrawList() };
			draw_list.AddRectFilled(min, max, to_u32({ 0.0f, 0.0f, 0.0f, 0.6f }));

			add_centered_text(
				draw_list,
				end_title_size,
				{ center.x, center.y - end_title_size / 2.0f },
				title,
				to_u32(title_color));
			add_centered_text(
				draw_list,
				end_hint_size,
				{ center.x, center.y + end_hint_size },
				hint);

			if (second_hint)
			{
				add_centered_text(
					draw_list,
					end_hint_size,
					{ center.x, center.y + 2.5f * end_hint_size },
					second_hint);
			}
		}

		// As minutes and seconds, like 1:05
		[[nodiscard]] std::string clear_time_text(float seconds)
		{
			const auto whole_seconds { static_cast<int>(seconds) };

			return std::format("Cleared in {}:{:02}", whole_seconds / 60, whole_seconds % 60);
		}
	}

	// ***

	void show_hud(
		const Player& player,
		float time_seconds,
		const SelectedEnemy& selected,
		const Level& level,
		const LevelProgress& progress)
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
			show_level_line(level, progress);
			show_health_bar(player);

			for (std::size_t index { 0 }; index < hotbar_slots.size(); ++index)
			{
				if (index != 0)
				{
					ImGui::SameLine();
				}

				show_slot(hotbar_slots[index], player, time_seconds, selected);
			}
		}
		ImGui::End();

		ImGui::PopStyleVar();

		if (player.is_dead())
		{
			show_end_screen("YOU DIED", srgb_color(0.8f, 0.1f, 0.08f), "Press R to try again");

			return;
		}

		show_selected_enemy(selected, player.position());

		if (progress.clear_seconds)
		{
			const std::string time_text { clear_time_text(*progress.clear_seconds) };
			show_end_screen(
				"LEVEL CLEARED",
				srgb_color(1.0f, 0.8f, 0.3f),
				time_text.c_str(),
				"R plays it again, F3 goes to the next level");
		}
	}
}
