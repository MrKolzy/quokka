#include "quokka/engine.hpp"

namespace quokka
{
	void Engine::run()
	{
		main_loop();
	}

	void Engine::recreate_swapchain()
	{
		// A minimized window has an empty framebuffer, so wait until it's restored
		auto framebuffer_extent { window_.framebuffer_extent() };
		while (framebuffer_extent.width == 0 || framebuffer_extent.height == 0)
		{
			// The window may be closed while minimized, in which case the main loop exits without recreating anything
			if (window_.should_close())
			{
				return;
			}

			window_.wait_events();
			framebuffer_extent = window_.framebuffer_extent();
		}

		// The new swapchain queries the current size, so any resize up to this point is already handled
		window_.clear_resized();

		// Nothing may still be using the old swapchain or its image views
		device_.logical_device().waitIdle();

		swapchain_.recreate();
	}

	void Engine::main_loop()
	{
		while (!window_.should_close())
		{
			window_.poll_events();

			const bool is_swapchain_outdated { renderer_.draw_frame() };

			// The resize flag is also checked, since not every driver reports out of date after a resize
			if (is_swapchain_outdated || window_.was_resized())
			{
				recreate_swapchain();
			}
		}

		// Drawing and presentation are asynchronous, so wait for them before destroying anything
		device_.logical_device().waitIdle();
	}
}
