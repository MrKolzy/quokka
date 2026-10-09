#include "quokka/engine.hpp"

#include "quokka/constants.hpp"
#include "quokka/light_panel.hpp"

#include <algorithm> // std::clamp, std::min
#include <chrono> // std::chrono::duration, std::chrono::steady_clock
#include <cmath> // std::cos, std::pow, std::sin
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

		// The follow camera's fixed angle, where yaw -45 degrees looks along -x and -z,
		// the diagonal from the player's start to the middle of the grid
		constexpr float follow_yaw { glm::radians(-45.0f) };
		constexpr float follow_pitch { glm::radians(-50.0f) };

		// In world units, around the chest of the knight, which is about 2.5 units tall
		constexpr float follow_target_height { 1.2f };

		// In world units, how close and how far the mouse wheel can zoom
		constexpr float min_camera_distance { 8.0f };
		constexpr float max_camera_distance { 80.0f };

		// Each wheel notch scales the distance by this, so zooming feels the same at any distance
		constexpr float zoom_step { 0.9f };

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

			update_player(delta_time.count());
			update_camera(delta_time.count());
			update_aim();
			update_enemies(delta_time.count());
			update_test_hits(elapsed_time.count());

			const bool is_swapchain_outdated {
				renderer_.draw_frame(camera_, light_, player_, elapsed_time.count())
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

	void Engine::update_player(float delta_seconds) noexcept
	{
		// The free camera takes the movement keys, and typing into the interface mustn't move
		if (is_camera_free_ || gui_.wants_keyboard())
		{
			return;
		}

		// Along the ground, as world x and z
		const glm::vec3 view_direction { camera_.forward() };
		const glm::vec2 forward {
			glm::normalize(glm::vec2 { view_direction.x, view_direction.z })
		};
		const glm::vec2 right { -forward.y, forward.x };

		const glm::vec2 direction {
			right * key_axis(window_, Key::d, Key::a) + forward * key_axis(window_, Key::w, Key::s)
		};

		// Normalized, so walking diagonally isn't faster
		const bool is_walking { direction != glm::vec2 { 0.0f } };
		player_.move(is_walking ? glm::normalize(direction) : direction, delta_seconds);
	}

	void Engine::update_camera(float delta_seconds) noexcept
	{
		// Ignored while typing into the interface, like the movement keys
		const bool was_camera_key_just_pressed {
			is_key_just_pressed(window_, Key::f2, was_camera_key_pressed_)
		};
		if (was_camera_key_just_pressed && !gui_.wants_keyboard())
		{
			is_camera_free_ = !is_camera_free_;
		}

		if (is_camera_free_)
		{
			update_free_camera(delta_seconds);

			return;
		}

		// The free camera may have been looking around when it was switched off
		if (window_.is_cursor_captured())
		{
			window_.set_cursor_captured(false);
		}

		// Scrolling away from the user zooms in, and over the interface it scrolls the interface
		if (!gui_.wants_mouse())
		{
			camera_distance_ = std::clamp(
				camera_distance_ * std::pow(zoom_step, window_.scroll_offset()),
				min_camera_distance,
				max_camera_distance);
		}

		camera_ = follow_camera(player_.position(), camera_distance_);
	}

	Camera Engine::follow_camera(glm::vec2 player_position, float distance) noexcept
	{
		const glm::vec3 target { player_position.x, follow_target_height, player_position.y };

		// Where the camera looks, from its yaw and pitch the same way as Camera::forward
		const glm::vec3 view_direction {
			std::cos(follow_pitch) * std::sin(follow_yaw),
			std::sin(follow_pitch),
			-std::cos(follow_pitch) * std::cos(follow_yaw)
		};

		return { target - view_direction * distance, target };
	}

	void Engine::update_aim() noexcept
	{
		// The free camera is for looking around, and over the interface the mouse is for it
		if (is_camera_free_ || gui_.wants_mouse())
		{
			return;
		}

		const vk::Extent2D extent { swapchain_.extent() };
		const float aspect_ratio {
			static_cast<float>(extent.width) / static_cast<float>(extent.height)
		};

		// Window y grows downward, but in normalized device coordinates it points up
		const glm::vec2 cursor { window_.cursor_position() };
		const glm::vec2 point { 2.0f * cursor.x - 1.0f, 1.0f - 2.0f * cursor.y };

		const glm::vec3 origin { camera_.position() };
		const glm::vec3 direction { camera_.ray_direction(point, aspect_ratio) };

		// A ray that doesn't point down never reaches the ground, so the last aim point stays
		if (direction.y >= 0.0f)
		{
			return;
		}

		// Where the ray's height reaches 0
		const glm::vec3 ground_point { origin - direction * (origin.y / direction.y) };
		player_.aim_at({ ground_point.x, ground_point.z });
	}

	void Engine::update_free_camera(float delta_seconds) noexcept
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
		// Ignored while typing into the interface, like the movement keys
		if (is_key_just_pressed(window_, Key::r, was_reset_key_pressed_) && !gui_.wants_keyboard())
		{
			// The player goes back too, or the enemies would start chasing it right away
			enemy_simulation_.reset();
			player_ = Player { EnemySimulation::player_start() };
		}

		enemy_simulation_.prepare_update(
			player_.position(),
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
