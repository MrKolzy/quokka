#include "quokka/engine.hpp"

#include "quokka/constants.hpp"

#include <format> // std::format

namespace quokka
{
	void Engine::run()
	{
		while (!window_.should_close())
		{
			window_.poll_events();

			const bool is_swapchain_outdated { renderer_.draw_frame() };

			// Not every platform reports out of date after a resize
			if (is_swapchain_outdated || window_.was_resized())
			{
				recreate_swapchain();
			}

			if constexpr (constants::should_show_frame_stats)
			{
				if (frame_timer_.tick())
				{
					window_.set_title(std::format("{} | {:.0f} FPS | {:.2f} ms",
						constants::app_name, frame_timer_.average_fps(),
						frame_timer_.average_frame_time_ms()));
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
