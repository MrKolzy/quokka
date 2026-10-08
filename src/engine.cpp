#include "quokka/engine.hpp"

#include "quokka/constants.hpp"

#include <chrono> // std::chrono::duration, std::chrono::steady_clock
#include <format> // std::format

namespace quokka
{
	void Engine::run()
	{
		while (!window_.should_close())
		{
			window_.poll_events();

			// Measured once, so the particles and every recording thread use the same time
			const auto frame_time { std::chrono::steady_clock::now() };

			// In seconds, as floats so the motion is smooth
			const std::chrono::duration<float> elapsed_time { frame_time - start_time_ };
			const std::chrono::duration<float> delta_time { frame_time - previous_frame_time_ };
			previous_frame_time_ = frame_time;

			const bool is_swapchain_outdated {
				renderer_.draw_frame(elapsed_time.count(), delta_time.count())
			};

			// Not every platform reports out of date after a resize
			if (is_swapchain_outdated || window_.was_resized())
			{
				recreate_swapchain();
			}

			if constexpr (constants::should_show_frame_stats)
			{
				if (frame_timer_.tick())
				{
					const auto gpu_times { gpu_timer_.take_average_times() };

					window_.set_title(std::format(
						"{} | {:.0f} FPS | {:.2f} ms | GPU {:.2f} ms, update {:.2f}, "
						"objects {:.2f}, particles {:.2f}",
						constants::app_name, frame_timer_.average_fps(),
						frame_timer_.average_frame_time_ms(), gpu_times.total_ms,
						gpu_times.particle_update_ms, gpu_times.object_draws_ms,
						gpu_times.particle_draws_ms));
				}
			}
		}
	}

	void Engine::recreate_swapchain()
	{
		// A minimized window has a 0x0 framebuffer, which a swapchain can't have
		auto framebuffer_extent { window_.framebuffer_extent() };
		while (framebuffer_extent.width == 0 || framebuffer_extent.height == 0)
		{
			// Otherwise closing while minimized would get stuck here
			if (window_.should_close())
			{
				return;
			}

			window_.wait_events();
			framebuffer_extent = window_.framebuffer_extent();
		}

		window_.clear_resized();

		// The GPU may still be using the old swapchain
		device_.logical_device().waitIdle();

		swapchain_.recreate();
	}
}
