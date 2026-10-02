#pragma once

#include <chrono>
#include <cstdint> // std::uint32_t

namespace quokka
{
	// Measures frame times and averages them over fixed intervals
	class FrameTimer
	{
	public:
		// Called once per frame, returns true when a new average is ready
		[[nodiscard]] bool tick() noexcept;

		[[nodiscard]] double average_frame_time_ms() const noexcept;
		[[nodiscard]] double average_fps() const noexcept;

	private:
		using Clock = std::chrono::steady_clock;

		Clock::time_point interval_start_ { Clock::now() };
		std::uint32_t frame_count_ {};
		double average_frame_time_ms_ {};
	};
}
