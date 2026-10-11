#pragma once

#include <array> // std::array
#include <chrono> // std::chrono::duration
#include <cstddef> // std::size_t
#include <cstdint> // std::uint32_t
#include <string> // std::string

namespace quokka
{
	class Device;
	class Swapchain;

	struct FrameAverages
	{
		double fps;
		double frame_time_ms;
		double gpu_time_ms;

		// The part of the GPU time spent simulating the enemies
		double enemy_update_ms;
	};

	class StatsOverlay
	{
	public:
		StatsOverlay(const Device& device, const Swapchain& swapchain);

		// Called once per frame, for the frame time graph
		void add_frame_time(std::chrono::duration<float> frame_time) noexcept;

		// Called whenever the frame timer has a new average
		void set_averages(const FrameAverages& averages) noexcept;

		// Between Gui::begin_frame and Gui::end_frame, with the current level's enemy count
		void show(std::uint32_t enemy_count) const;

	private:
		void show_contents(std::uint32_t enemy_count) const;

		// Enough to spot hitches, and each one still gets a point of the graph
		static constexpr std::size_t history_size { 300 };

		const Swapchain& swapchain_;

		// Read once, since the GPU never changes
		std::string gpu_name_;

		FrameAverages averages_ {};

		// A ring buffer in milliseconds, where next_index_ holds the oldest one
		std::array<float, history_size> frame_times_ms_ {};
		std::size_t next_index_ {};
	};
}
