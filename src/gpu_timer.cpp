#include "quokka/gpu_timer.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"

#include <array> // std::array
#include <chrono> // std::chrono::duration
#include <cstdint> // std::uint64_t
#include <ratio> // std::milli, std::nano
#include <stdexcept> // std::runtime_error

namespace quokka
{
	namespace
	{
		constexpr auto timestamp_count { static_cast<std::uint32_t>(GpuTimestamp::count) };

		vk::raii::QueryPool create_query_pool(const vk::raii::Device& device)
		{
			const vk::QueryPoolCreateInfo create_info {
				.queryType { vk::QueryType::eTimestamp },
				// Each frame in flight writes its own, so one can be read while another is written
				.queryCount { timestamp_count * constants::max_frames_in_flight }
			};

			return device.createQueryPool(create_info);
		}
	}

	// ***

	GpuTimer::GpuTimer(const Device& device)
		: timestamp_period_ { device.physical_device().getProperties().limits.timestampPeriod }
		, query_pool_ { create_query_pool(device.logical_device()) }
	{
		// Queries start undefined, and reset ones read as not ready until a frame writes them
		query_pool_.reset(0, timestamp_count * constants::max_frames_in_flight);
	}

	void GpuTimer::record_timestamp(
		const vk::raii::CommandBuffer& command_buffer,
		std::uint32_t frame_index,
		GpuTimestamp timestamp) const
	{
		const std::uint32_t query {
			frame_index * timestamp_count + static_cast<std::uint32_t>(timestamp)
		};

		// Written once every earlier command finishes, whatever stage it's in
		command_buffer.writeTimestamp2(
			vk::PipelineStageFlagBits2::eAllCommands, *query_pool_, query);
	}

	void GpuTimer::collect(std::uint32_t frame_index)
	{
		const std::uint32_t first_query { frame_index * timestamp_count };
		std::array<std::uint64_t, timestamp_count> timestamps {};

		// Without waiting, because a frame that was never submitted would never be ready
		const auto result {
			query_pool_.getResults(
				first_query,
				timestamp_count,
				sizeof(timestamps),
				timestamps.data(),
				sizeof(std::uint64_t),
				vk::QueryResultFlagBits::e64)
		};
		if (result == vk::Result::eNotReady)
		{
			return;
		}
		if (result != vk::Result::eSuccess)
		{
			throw std::runtime_error("Failed to read the GPU timestamps");
		}

		// Ready to be written again by the next submission of this frame
		query_pool_.reset(first_query, timestamp_count);

		const auto milliseconds_between {
			[&timestamps, this](GpuTimestamp start, GpuTimestamp end)
			{
				const std::uint64_t ticks {
					timestamps[static_cast<std::uint32_t>(end)] -
					timestamps[static_cast<std::uint32_t>(start)]
				};
				const std::chrono::duration<double, std::nano> elapsed {
					static_cast<double>(ticks) * timestamp_period_
				};

				return std::chrono::duration<double, std::milli> { elapsed }.count();
			}
		};

		time_sums_.particle_update_ms +=
			milliseconds_between(GpuTimestamp::frame_start, GpuTimestamp::particles_updated);
		time_sums_.object_draws_ms +=
			milliseconds_between(GpuTimestamp::particles_updated, GpuTimestamp::objects_drawn);
		time_sums_.particle_draws_ms +=
			milliseconds_between(GpuTimestamp::objects_drawn, GpuTimestamp::frame_end);
		time_sums_.total_ms +=
			milliseconds_between(GpuTimestamp::frame_start, GpuTimestamp::frame_end);
		++collected_frame_count_;
	}

	GpuFrameTimes GpuTimer::take_average_times() noexcept
	{
		// Happens until the first frames are collected
		if (collected_frame_count_ == 0)
		{
			return {};
		}

		const auto frame_count { static_cast<double>(collected_frame_count_) };
		const GpuFrameTimes average_times {
			.particle_update_ms { time_sums_.particle_update_ms / frame_count },
			.object_draws_ms { time_sums_.object_draws_ms / frame_count },
			.particle_draws_ms { time_sums_.particle_draws_ms / frame_count },
			.total_ms { time_sums_.total_ms / frame_count }
		};

		time_sums_ = {};
		collected_frame_count_ = 0;

		return average_times;
	}
}
