#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t

namespace quokka
{
	class Device;

	enum class GpuTimestamp : std::uint32_t
	{
		// In the order they're written every frame, so each phase lasts until the next one
		frame_start,
		frame_end,

		// Not a timestamp, but how many there are
		count
	};

	struct GpuFrameTimes
	{
		double total_ms;
	};

	class GpuTimer
	{
	public:
		explicit GpuTimer(const Device& device);
		~GpuTimer() = default;

		// Renderer keeps a reference to it, so it can't be copied or moved
		GpuTimer(const GpuTimer&) = delete;
		GpuTimer& operator=(const GpuTimer&) = delete;
		GpuTimer(GpuTimer&&) = delete;
		GpuTimer& operator=(GpuTimer&&) = delete;

		void record_timestamp(
			const vk::raii::CommandBuffer& command_buffer,
			std::uint32_t frame_index,
			GpuTimestamp timestamp) const;

		// Must be called after waiting for the frame's fence, so its last timestamps are written
		void collect(std::uint32_t frame_index);

		// Averages the frames collected since the previous call, and starts a new average
		[[nodiscard]] GpuFrameTimes take_average_times() noexcept;

	private:
		// Nanoseconds per timestamp tick
		float timestamp_period_;

		// One set of timestamps per frame in flight
		vk::raii::QueryPool query_pool_;

		// Summed over the frames collected since the last average
		GpuFrameTimes time_sums_ {};
		std::uint32_t collected_frame_count_ {};
	};
}
