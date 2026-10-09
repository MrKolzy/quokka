#include "quokka/stats_overlay.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/swapchain.hpp"

#include <imgui.h>

#include <algorithm> // std::clamp, std::ranges::max
#include <format> // std::format
#include <ratio> // std::milli

namespace quokka
{
	namespace
	{
		// See through, so the scene stays visible behind it
		constexpr float background_alpha { 0.6f };

		// From the top left corner, like the overlays of most games
		constexpr ImVec2 corner_offset { 10.0f, 10.0f };

		// One pixel per frame of the history, and tall enough to tell small changes apart
		constexpr ImVec2 graph_size { 300.0f, 60.0f };

		// Room above the slowest frame, so its peak doesn't touch the top edge
		constexpr float graph_headroom { 1.25f };

		// The longest label of both tables
		constexpr const char* recording_threads_label { "Recording threads" };

		// Both tables give their labels the width of the longest one, so their values line up
		[[nodiscard]] bool begin_table(const char* id)
		{
			if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit))
			{
				return false;
			}

			ImGui::TableSetupColumn(
				"label",
				ImGuiTableColumnFlags_WidthFixed,
				ImGui::CalcTextSize(recording_threads_label).x);
			ImGui::TableSetupColumn("value");

			return true;
		}

		// A label in the first column and its value in the second
		void table_row(const char* label, const std::string& value)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(value.c_str());
		}

		[[nodiscard]] std::string msaa_text(vk::SampleCountFlagBits sample_count)
		{
			if (sample_count == vk::SampleCountFlagBits::e1)
			{
				return "Off";
			}

			return std::format("{}x", vk::to_string(sample_count));
		}
	}

	// ***

	StatsOverlay::StatsOverlay(const Device& device, const Swapchain& swapchain)
		: swapchain_ { swapchain }
		, gpu_name_ { device.physical_device().getProperties().deviceName.data() }
	{
	}

	void StatsOverlay::add_frame_time(std::chrono::duration<float> frame_time) noexcept
	{
		const std::chrono::duration<float, std::milli> frame_time_ms { frame_time };
		frame_times_ms_[next_index_] = frame_time_ms.count();
		next_index_ = (next_index_ + 1) % history_size;
	}

	void StatsOverlay::set_averages(const FrameAverages& averages) noexcept
	{
		averages_ = averages;
	}

	void StatsOverlay::show() const
	{
		const ImGuiViewport& viewport { *ImGui::GetMainViewport() };

		// Only the first time, so it can still be dragged elsewhere
		ImGui::SetNextWindowPos(
			{ viewport.WorkPos.x + corner_offset.x, viewport.WorkPos.y + corner_offset.y },
			ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(background_alpha);

		// Sized to its contents, and never focused on its own, so it doesn't take the keyboard
		constexpr ImGuiWindowFlags window_flags {
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav
		};

		// Begin returns false while collapsed, but End must be called either way
		if (ImGui::Begin("Stats", nullptr, window_flags))
		{
			show_contents();
		}
		ImGui::End();
	}

	void StatsOverlay::show_contents() const
	{
		ImGui::TextUnformatted(gpu_name_.c_str());

		// The conditions of the measurement, so a screenshot of the overlay documents them
		if (begin_table("settings"))
		{
			const vk::Extent2D extent { swapchain_.extent() };
			table_row("Resolution", std::format("{}x{}", extent.width, extent.height));
			table_row("Present mode", vk::to_string(swapchain_.present_mode()));
			table_row("MSAA", msaa_text(swapchain_.sample_count()));
			table_row(recording_threads_label, std::format("{}", constants::recording_thread_count));
			table_row("Enemies", std::format("{}", constants::enemy_count));
			ImGui::EndTable();
		}

		ImGui::Separator();

		// Averaged over the last second, like the window title
		if (begin_table("averages"))
		{
			table_row("FPS", std::format("{:.0f}", averages_.fps));
			table_row("Frame", std::format("{:.2f} ms", averages_.frame_time_ms));
			table_row("GPU", std::format("{:.2f} ms", averages_.gpu_time_ms));
			table_row("Enemy update", std::format("{:.3f} ms", averages_.enemy_update_ms));
			ImGui::EndTable();
		}

		// Near full means the GPU sets the pace, otherwise something else does, such as the CPU
		// or presentation, so a faster GPU wouldn't raise the frame rate
		// Clamped, because the two averages cover slightly different frames
		const double gpu_share {
			averages_.frame_time_ms > 0.0
				? std::clamp(averages_.gpu_time_ms / averages_.frame_time_ms, 0.0, 1.0)
				: 0.0
		};
		ImGui::ProgressBar(
			static_cast<float>(gpu_share),
			{ graph_size.x, 0.0f },
			std::format("GPU busy {:.0f}%", gpu_share * 100.0).c_str());
		ImGui::SetItemTooltip("Share of each frame the GPU spends drawing it");

		ImGui::Separator();

		// Every frame, so it shows the hitches that the averages hide
		ImGui::TextUnformatted(std::format("Frame time, last {} frames", history_size).c_str());
		const float slowest_frame_ms { std::ranges::max(frame_times_ms_) };
		ImGui::PlotLines(
			"##frame_times",
			frame_times_ms_.data(),
			static_cast<int>(history_size),
			static_cast<int>(next_index_),
			std::format("max {:.2f} ms", slowest_frame_ms).c_str(),
			0.0f,
			slowest_frame_ms * graph_headroom,
			graph_size);

		ImGui::TextDisabled("WASD walks, wheel zooms, Space hits, R resets");
		ImGui::TextDisabled("F1 hides the interface, F2 frees the camera");
	}
}
