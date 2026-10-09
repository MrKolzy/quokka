#include "quokka/engine.hpp"

#include "quokka/constants.hpp"

#include <chrono> // std::chrono::duration, std::chrono::steady_clock
#include <format> // std::format

namespace quokka
{
	namespace
	{
		// In world units per second, the copies of the grid are 2 units apart
		constexpr float camera_speed { 5.0f };

		// In radians per pixel, without the delta time because the offset already is a distance
		constexpr float mouse_sensitivity { 0.002f };

		// 1 or -1, or 0 when both keys or neither are pressed
		[[nodiscard]] float key_axis(
			const Window& window,
			Key positive_key,
			Key negative_key) noexcept
		{
			return static_cast<float>(window.is_key_pressed(positive_key)) -
				static_cast<float>(window.is_key_pressed(negative_key));
		}
	}

	// ***

	void Engine::run()
	{
		while (!window_.should_close())
		{
			window_.poll_events();

			const auto frame_time { std::chrono::steady_clock::now() };

			// In seconds, as a float so the motion is smooth
			const std::chrono::duration<float> delta_time { frame_time - previous_frame_time_ };
			previous_frame_time_ = frame_time;

			stats_overlay_.add_frame_time(delta_time);

			// Every ImGui call between the two adds to this frame's interface
			gui_.begin_frame();

			if (gui_.is_visible())
			{
				stats_overlay_.show();
			}

			gui_.end_frame();

			update_camera(delta_time.count());

			const bool is_swapchain_outdated { renderer_.draw_frame(camera_) };

			// Not every platform reports out of date after a resize
			if (is_swapchain_outdated || window_.was_resized())
			{
				recreate_swapchain();
			}

			if (frame_timer_.tick())
			{
				const FrameAverages averages {
					.fps { frame_timer_.average_fps() },
					.frame_time_ms { frame_timer_.average_frame_time_ms() },
					.gpu_time_ms { gpu_timer_.take_average_times().total_ms }
				};

				stats_overlay_.set_averages(averages);

				if constexpr (constants::should_show_frame_stats)
				{
					window_.set_title(std::format(
						"{} | {:.0f} FPS | {:.2f} ms | GPU {:.2f} ms",
						constants::app_name, averages.fps,
						averages.frame_time_ms, averages.gpu_time_ms));
				}
			}
		}
	}

	void Engine::update_camera(float delta_seconds) noexcept
	{
		// Held to look around like in the Unity, Unreal and Godot editors, so the cursor is free
		// Pressed over the interface it's for the interface, while looking ImGui ignores the mouse
		const bool is_looking { window_.is_right_mouse_button_pressed() && !gui_.wants_mouse() };
		if (is_looking != window_.is_cursor_captured())
		{
			// This frame's offset was measured before the change, so it's skipped
			window_.set_cursor_captured(is_looking);
		}
		else if (is_looking)
		{
			const glm::vec2 cursor_offset { window_.cursor_offset() };

			// Screen y grows downward, so moving the cursor up has to raise the pitch
			camera_.rotate(
				cursor_offset.x * mouse_sensitivity,
				-cursor_offset.y * mouse_sensitivity
			);
		}

		// Typing into the interface mustn't move the camera
		if (gui_.wants_keyboard())
		{
			return;
		}

		// Relative to the view, as move expects
		const glm::vec3 direction {
			key_axis(window_, Key::d, Key::a),
			key_axis(window_, Key::e, Key::q),
			key_axis(window_, Key::w, Key::s)
		};

		// Normalized, so moving diagonally isn't faster
		if (direction != glm::vec3 { 0.0f })
		{
			camera_.move(glm::normalize(direction) * camera_speed * delta_seconds);
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
