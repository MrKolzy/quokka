#include "quokka/engine.hpp"

#include "quokka/constants.hpp"
#include "quokka/light_panel.hpp"

#include <algorithm> // std::min
#include <chrono> // std::chrono::duration, std::chrono::steady_clock
#include <cstdint> // std::uint32_t
#include <format> // std::format
#include <random> // std::bernoulli_distribution, std::uniform_int_distribution

namespace quokka
{
	namespace
	{
		// In world units per second, the copies of the grid are 2 units apart
		constexpr float camera_speed { 5.0f };

		// In seconds, longer frames such as the one after a resize are simulated as this long,
		// so the enemies don't jump
		constexpr float max_simulation_delta { 0.1f };

		// Critical hits deal double damage
		constexpr double critical_chance { 0.15 };
		constexpr std::uint32_t min_damage { 100 };
		constexpr std::uint32_t max_damage { 999 };

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

		// True only on the frame the key goes down, and was_pressed remembers it for the next one
		[[nodiscard]] bool is_key_just_pressed(
			const Window& window,
			Key key,
			bool& was_pressed) noexcept
		{
			const bool is_pressed { window.is_key_pressed(key) };
			const bool is_just_pressed { is_pressed && !was_pressed };
			was_pressed = is_pressed;

			return is_just_pressed;
		}
	}

	// ***

	void Engine::run()
	{
		while (!window_.should_close())
		{
			window_.poll_events();

			const auto frame_time { std::chrono::steady_clock::now() };

			// In seconds, as floats so the motion is smooth
			const std::chrono::duration<float> elapsed_time { frame_time - start_time_ };
			const std::chrono::duration<float> delta_time { frame_time - previous_frame_time_ };
			previous_frame_time_ = frame_time;

			stats_overlay_.add_frame_time(delta_time);

			// Every ImGui call between the two adds to this frame's interface
			gui_.begin_frame();

			if (gui_.is_visible())
			{
				stats_overlay_.show();
				show_light_panel(light_);
			}

			gui_.end_frame();

			update_camera(delta_time.count());
			update_enemies(delta_time.count());
			update_test_hits(elapsed_time.count());

			const bool is_swapchain_outdated {
				renderer_.draw_frame(camera_, light_, elapsed_time.count())
			};

			// Not every platform reports out of date after a resize
			if (is_swapchain_outdated || window_.was_resized())
			{
				recreate_swapchain();
			}

			if (frame_timer_.tick())
			{
				const GpuFrameTimes gpu_times { gpu_timer_.take_average_times() };
				const FrameAverages averages {
					.fps { frame_timer_.average_fps() },
					.frame_time_ms { frame_timer_.average_frame_time_ms() },
					.gpu_time_ms { gpu_times.total_ms },
					.enemy_update_ms { gpu_times.enemy_update_ms }
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

	void Engine::update_enemies(float delta_seconds) noexcept
	{
		// Where the view meets the ground, which never happens at or above the horizon
		const glm::vec3 forward { camera_.forward() };
		if (forward.y < 0.0f)
		{
			const glm::vec3 position { camera_.position() };
			const glm::vec3 ground_point { position - forward * (position.y / forward.y) };
			chase_target_ = { ground_point.x, ground_point.z };
		}

		// Ignored while typing into the interface, like the camera keys
		if (is_key_just_pressed(window_, Key::r, was_reset_key_pressed_) && !gui_.wants_keyboard())
		{
			enemy_simulation_.reset();
		}

		enemy_simulation_.prepare_update(
			chase_target_,
			std::min(delta_seconds, max_simulation_delta));
	}

	void Engine::update_test_hits(float time_seconds)
	{
		// Ignored while typing into the interface, like the camera keys
		const bool was_hit_key_just_pressed {
			is_key_just_pressed(window_, Key::space, was_hit_key_pressed_)
		};
		if (!was_hit_key_just_pressed || gui_.wants_keyboard())
		{
			return;
		}

		std::uniform_int_distribution<std::uint32_t> damage_distribution { min_damage, max_damage };
		std::bernoulli_distribution critical_distribution { critical_chance };

		for (std::uint32_t index { 0 }; index < constants::enemy_count; ++index)
		{
			const bool is_critical { critical_distribution(random_engine_) };
			const std::uint32_t damage { damage_distribution(random_engine_) };

			damage_numbers_.spawn({
				.enemy_index { index },
				.spawn_time { time_seconds },
				.value { is_critical ? 2 * damage : damage },
				.is_critical { is_critical ? 1u : 0u }
			});
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
