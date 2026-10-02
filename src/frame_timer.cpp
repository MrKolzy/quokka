#include "quokka/frame_timer.hpp"

#include <chrono>

namespace quokka
{
	namespace
	{
		// Long enough for a stable average, short enough to react to changes
		constexpr std::chrono::seconds averaging_interval { 1 };
	}

	// ***

	bool FrameTimer::tick() noexcept
	{
		++frame_count_;

		const auto now { Clock::now() };
		const auto elapsed { now - interval_start_ };
		if (elapsed < averaging_interval)
		{
			return false;
		}

		// The total time is averaged, not the per-frame rates, so each frame weighs as much as it lasted
		average_frame_time_ms_ = std::chrono::duration<double, std::milli> { elapsed }.count() / frame_count_;

		interval_start_ = now;
		frame_count_ = 0;

		return true;
	}

	double FrameTimer::average_frame_time_ms() const noexcept
	{
		return average_frame_time_ms_;
	}

	double FrameTimer::average_fps() const noexcept
	{
		return 1000.0 / average_frame_time_ms_;
	}
}
