#include "quokka/engine.hpp"

#include "quokka/abilities.hpp"
#include "quokka/constants.hpp"
#include "quokka/hud.hpp"
#include "quokka/light_panel.hpp"

#include <algorithm> // std::clamp, std::max, std::min
#include <chrono> // std::chrono::duration, std::chrono::steady_clock
#include <cmath> // std::cos, std::pow, std::sin
#include <cstddef> // std::size_t
#include <cstdint> // std::uint32_t
#include <format> // std::format

namespace quokka
{
	namespace
	{
		// In world units per second, the copies of the grid are 2 units apart
		constexpr float camera_speed { 5.0f };

		// In seconds, longer frames such as the one after a resize are simulated as this long,
		// so the enemies don't jump
		constexpr float max_simulation_delta { 0.1f };

		// In radians per pixel, without the delta time because the offset already is a distance
		constexpr float mouse_sensitivity { 0.002f };

		// In seconds, how long the game freezes when the abilities hit, a bit longer the more they
		// hit, so a blast into a crowd lands harder than a single strike
		constexpr float hit_stop_base { 0.04f };
		constexpr float hit_stop_per_hit { 0.003f };
		constexpr float max_hit_stop { 0.1f };

		// Every time Whirlwind changes direction it freezes and kicks the camera, an impact frame
		constexpr float reversal_hit_stop { 0.06f };
		constexpr float trauma_per_reversal { 0.35f };

		// The landing freezes the longest and shakes the camera as hard as it goes
		constexpr float landing_hit_stop { 0.15f };

		// In seconds after the landing, until the last wave and the readback of its hits, which
		// arrives a few frames late, so the waves shake the camera without stuttering the game
		constexpr float quake_duration { quake_last_front / quake_wave_speed + 0.1f };

		// In world units, how far the first seismic wave has spread this long after the cast
		[[nodiscard]] float quake_front(float whirlwind_age) noexcept
		{
			return std::clamp(
				(whirlwind_age - whirlwind_land_time) * quake_wave_speed,
				0.0f,
				quake_last_front);
		}

		// True when the time went past the moment, from the previous age to this one
		[[nodiscard]] bool has_reached(float previous_age, float age, float moment) noexcept
		{
			return previous_age < moment && age >= moment;
		}

		// The camera shakes with its trauma, from 0 to 1, which every impact raises, a bit more
		// for every enemy hit, and which fades per second
		constexpr float trauma_per_impact { 0.2f };
		constexpr float trauma_per_hit { 0.015f };
		constexpr float trauma_decay { 1.5f };

		// In world units at full trauma, and in radians per second, how fast it shakes
		constexpr float max_shake_offset { 0.45f };
		constexpr float shake_frequency { 25.0f };

		// Dragging tilts the follow camera between almost along the ground and almost straight down
		constexpr float min_follow_pitch { glm::radians(-85.0f) };
		constexpr float max_follow_pitch { glm::radians(-15.0f) };

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

			// Frozen during a hit-stop, so everything the game animates stops at once, while the
			// camera and the interface keep the real time
			update_impact_feedback();
			const float game_delta { hit_stop_left_ > 0.0f ? 0.0f : delta_time.count() };
			hit_stop_left_ = std::max(hit_stop_left_ - delta_time.count(), 0.0f);
			game_time_ += game_delta;

			// Every ImGui call between the two adds to this frame's interface
			gui_.begin_frame();

			if (gui_.are_panels_visible())
			{
				stats_overlay_.show(enemy_simulation_.enemy_count());
				show_light_panel(light_);
			}

			const LevelProgress progress {
				.enemies_left { enemy_simulation_.alive_count() },
				.clear_seconds { clear_seconds_ }
			};
			show_hud(
				player_,
				game_time_,
				enemy_simulation_.selected_enemy(),
				levels[level_index_],
				progress);

			gui_.end_frame();

			update_level(game_time_);
			update_selection();
			update_player(game_delta, game_time_);
			update_camera(delta_time.count(), elapsed_time.count());
			update_abilities(game_time_);
			update_void_sword(game_time_);
			update_whirlwind(game_time_);
			update_enemies(game_delta, game_time_);
			update_test_hits(game_time_);

			const bool is_swapchain_outdated {
				renderer_.draw_frame(camera_, light_, player_, game_time_)
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

	void Engine::update_player(float delta_seconds, float time_seconds) noexcept
	{
		// Read back from the frames the GPU finished since the last update
		const auto damage { static_cast<float>(enemy_simulation_.take_player_damage()) };
		player_.take_damage(damage, time_seconds);
		player_.regenerate(delta_seconds, time_seconds);

		// The free camera takes the movement keys, typing into the interface mustn't move,
		// and the dead don't walk, so the player stands still, which also stops its running
		if (is_camera_free_ || gui_.wants_keyboard() || player_.is_dead())
		{
			player_.move(glm::vec2 { 0.0f }, delta_seconds);

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

	void Engine::update_camera(float delta_seconds, float time_seconds) noexcept
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

		// Dragging circles the player, and screen y grows downward, so dragging up tilts it
		// toward the horizon
		const glm::vec2 look_offset { mouse_look_offset() };
		follow_yaw_ += look_offset.x * mouse_sensitivity;
		follow_pitch_ = std::clamp(
			follow_pitch_ - look_offset.y * mouse_sensitivity,
			min_follow_pitch,
			max_follow_pitch);

		// Scrolling away from the user zooms in, and over the interface it scrolls the interface
		if (!gui_.wants_mouse())
		{
			camera_distance_ = std::clamp(
				camera_distance_ * std::pow(zoom_step, window_.scroll_offset()),
				min_camera_distance,
				max_camera_distance);
		}

		// Sines of unrelated frequencies on each axis, smooth but never shaking in step,
		// and the square of the trauma, so light hits barely move the camera
		camera_trauma_ = std::max(camera_trauma_ - trauma_decay * delta_seconds, 0.0f);
		const float shake { camera_trauma_ * camera_trauma_ * max_shake_offset };
		const float phase { time_seconds * shake_frequency };
		const glm::vec3 shake_offset {
			std::sin(phase * 1.13f + 0.7f) * shake,
			std::sin(phase * 0.91f + 2.1f) * shake * 0.6f,
			std::sin(phase * 1.37f + 4.2f) * shake
		};

		camera_ = follow_camera(
			player_.position(),
			camera_distance_,
			follow_yaw_,
			follow_pitch_,
			shake_offset);
	}

	Camera Engine::follow_camera(
		glm::vec2 player_position,
		float distance,
		float yaw,
		float pitch,
		glm::vec3 shake_offset) noexcept
	{
		const glm::vec3 target { player_position.x, follow_target_height, player_position.y };

		// Where the camera looks, from its yaw and pitch the same way as Camera::forward
		const glm::vec3 view_direction {
			std::cos(pitch) * std::sin(yaw),
			std::sin(pitch),
			-std::cos(pitch) * std::cos(yaw)
		};

		return { target - view_direction * distance + shake_offset, target + shake_offset };
	}

	void Engine::update_impact_feedback() noexcept
	{
		// Read back from the frames the GPU finished since the last update, a few frames late
		const std::uint32_t hit_count { enemy_simulation_.take_hit_count() };
		if (hit_count == 0)
		{
			return;
		}

		// The waves hit a few enemies every frame as they pass, which would stutter if each froze
		const auto hits { static_cast<float>(hit_count) };
		const float time_since_landing { whirlwind_age_ - whirlwind_land_time };
		const bool is_quake_spreading {
			time_since_landing >= 0.0f && time_since_landing < quake_duration
		};
		if (!is_quake_spreading)
		{
			hit_stop_left_ = std::max(
				hit_stop_left_,
				std::min(hit_stop_base + hit_stop_per_hit * hits, max_hit_stop));
		}
		camera_trauma_ = std::min(camera_trauma_ + trauma_per_impact + trauma_per_hit * hits, 1.0f);
	}

	void Engine::update_void_sword(float time_seconds) noexcept
	{
		// Never thrown is long ago, past the landing, and a new throw starts over
		const float age { time_seconds - player_.last_strike_time() };
		const float previous_age { strike_age_ <= age ? strike_age_ : -1.0f };
		strike_age_ = age;

		if (has_reached(previous_age, age, hook_flight_duration))
		{
			enemy_simulation_.land_hook();
			damage_numbers_.note_cast(time_seconds);
		}
	}

	void Engine::update_whirlwind(float time_seconds) noexcept
	{
		// Never cast is long ago, past everything, and a new cast starts over from before its
		// first turn
		const float age { time_seconds - player_.last_blast_time() };
		const float previous_age { whirlwind_age_ <= age ? whirlwind_age_ : -1.0f };
		whirlwind_age_ = age;

		if (player_.is_dead())
		{
			return;
		}

		// The cast already hit for the first turn, so only the later ones hit again around
		// where the player is now, each with a reversal
		for (std::uint32_t turn { 1 }; turn < whirlwind_turns; ++turn)
		{
			const float turn_start { static_cast<float>(turn) * whirlwind_turn_duration };
			if (has_reached(previous_age, age, turn_start))
			{
				enemy_simulation_.cast(Ability::blast);
				damage_numbers_.note_cast(time_seconds);

				hit_stop_left_ = std::max(hit_stop_left_, reversal_hit_stop);
				camera_trauma_ = std::min(camera_trauma_ + trauma_per_reversal, 1.0f);
			}
		}

		if (has_reached(previous_age, age, whirlwind_land_time))
		{
			player_.slam();
			hit_stop_left_ = std::max(hit_stop_left_, landing_hit_stop);
			camera_trauma_ = 1.0f;
		}

		// Each wave hits the enemies its front passed since the last frame
		const float front { quake_front(age) };
		const float previous_front { quake_front(previous_age) };
		if (front > previous_front)
		{
			enemy_simulation_.spread_quake(player_.quake_point(), front, front - previous_front);
			damage_numbers_.note_cast(time_seconds);
		}
	}

	glm::vec2 Engine::mouse_look_offset() noexcept
	{
		// Held to look around like in the Unity, Unreal and Godot editors and in most third
		// person games, and the cursor is hidden meanwhile
		// Pressed over the interface it's for the interface, while looking ImGui ignores the mouse
		const bool is_looking {
			window_.is_mouse_button_pressed(MouseButton::right) && !gui_.wants_mouse()
		};
		if (is_looking != window_.is_cursor_captured())
		{
			// This frame's offset was measured before the change, so it's skipped
			window_.set_cursor_captured(is_looking);

			return glm::vec2 { 0.0f };
		}

		return is_looking ? window_.cursor_offset() : glm::vec2 { 0.0f };
	}

	void Engine::update_free_camera(float delta_seconds) noexcept
	{
		// Screen y grows downward, so moving the cursor up has to raise the pitch
		const glm::vec2 look_offset { mouse_look_offset() };
		camera_.rotate(look_offset.x * mouse_sensitivity, -look_offset.y * mouse_sensitivity);

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

	void Engine::update_selection() noexcept
	{
		const bool is_tab_pressed {
			is_key_just_pressed(window_, Key::tab, was_tab_key_pressed_)
		};
		const bool is_mouse_pressed { window_.is_mouse_button_pressed(MouseButton::left) };
		const bool is_click { is_mouse_pressed && !was_select_button_pressed_ };
		was_select_button_pressed_ = is_mouse_pressed;

		if (player_.is_dead())
		{
			return;
		}

		if (is_tab_pressed && !gui_.wants_keyboard())
		{
			enemy_simulation_.cycle_enemy();
		}

		if (!is_click || gui_.wants_mouse() || window_.is_cursor_captured())
		{
			return;
		}

		const auto extent { swapchain_.extent() };
		if (extent.width == 0 || extent.height == 0)
		{
			return;
		}

		const float aspect { static_cast<float>(extent.width) / static_cast<float>(extent.height) };
		const glm::vec2 cursor { window_.cursor_clip_position() };
		const glm::mat4 inverse_view_projection {
			glm::inverse(camera_.projection_matrix(aspect) * camera_.view_matrix())
		};
		const glm::vec4 far_point { inverse_view_projection * glm::vec4 { cursor, 1.0f, 1.0f } };
		const glm::vec3 direction {
			glm::normalize(glm::vec3 { far_point } / far_point.w - camera_.position())
		};
		enemy_simulation_.select_enemy(camera_.position(), direction);
	}

	void Engine::update_abilities(float time_seconds) noexcept
	{
		// The free camera moves with some of the keys, and typing into the interface mustn't cast
		if (is_camera_free_ || player_.is_dead() || gui_.wants_keyboard())
		{
			return;
		}

		for (const HotbarSlot& slot : hotbar_slots)
		{
			if (!slot.ability)
			{
				continue;
			}

			const Ability ability { *slot.ability };
			const bool is_pressed { window_.is_key_pressed(slot.key) };

			// Without a target in reach the sword isn't thrown and its cooldown doesn't start,
			// instead the ground shows how far it reaches
			const SelectedEnemy selected { enemy_simulation_.selected_enemy() };
			if (ability == Ability::strike && !can_throw_at(selected, player_.position()))
			{
				if (is_pressed)
				{
					player_.mark_strike_range(time_seconds);
				}

				continue;
			}

			// Held keys cast again as soon as the cooldown ends, like in Diablo
			if (is_pressed && player_.try_cast(ability, time_seconds))
			{
				enemy_simulation_.cast(ability);
				damage_numbers_.note_cast(time_seconds);
			}
		}
	}

	void Engine::update_level(float time_seconds) noexcept
	{
		// Ignored while typing into the interface, like the movement keys
		const bool is_reset_just_pressed {
			is_key_just_pressed(window_, Key::r, was_reset_key_pressed_)
		};
		const bool is_level_key_just_pressed {
			is_key_just_pressed(window_, Key::f3, was_level_key_pressed_)
		};
		if (!gui_.wants_keyboard())
		{
			if (is_reset_just_pressed)
			{
				start_level(level_index_, time_seconds);
			}
			else if (is_level_key_just_pressed)
			{
				start_level((level_index_ + 1) % levels.size(), time_seconds);
			}
		}

		// Dying with the last enemy still loses
		if (!clear_seconds_ && enemy_simulation_.alive_count() == 0 && !player_.is_dead())
		{
			clear_seconds_ = time_seconds - level_start_time_;
		}
	}

	void Engine::start_level(std::size_t level_index, float time_seconds) noexcept
	{
		// The player goes back too, or the enemies would start chasing it right away
		level_index_ = level_index;
		enemy_simulation_.start_level(level_index);
		player_ = Player { levels[level_index].player_start };
		level_start_time_ = time_seconds;
		clear_seconds_.reset();
	}

	void Engine::update_enemies(float delta_seconds, float time_seconds) noexcept
	{
		enemy_simulation_.prepare_update(
			player_.position(),
			player_.facing(),
			!player_.is_dead(),
			std::min(delta_seconds, max_simulation_delta),
			time_seconds);
	}

	void Engine::update_test_hits(float time_seconds) noexcept
	{
		// Ignored while typing into the interface, like the camera keys
		const bool was_hit_key_just_pressed {
			is_key_just_pressed(window_, Key::space, was_hit_key_pressed_)
		};
		if (!was_hit_key_just_pressed || gui_.wants_keyboard())
		{
			return;
		}

		enemy_simulation_.cast_test_hits();
		damage_numbers_.note_cast(time_seconds);
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
