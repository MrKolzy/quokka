#pragma once

#include "quokka/camera.hpp"
#include "quokka/constants.hpp"
#include "quokka/damage_numbers.hpp"
#include "quokka/device.hpp"
#include "quokka/enemy_simulation.hpp"
#include "quokka/frame_timer.hpp"
#include "quokka/gpu_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/gui.hpp"
#include "quokka/health_bars.hpp"
#include "quokka/level.hpp"
#include "quokka/light.hpp"
#include "quokka/player.hpp"
#include "quokka/renderer.hpp"
#include "quokka/stats_overlay.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/window.hpp"

#include <glm/glm.hpp>

#include <chrono> // std::chrono::steady_clock
#include <cstddef> // std::size_t
#include <optional> // std::optional

namespace quokka
{
	class Engine
	{
	public:
		Engine() = default;
		~Engine() = default;

		// Members reference each other, so it can't be copied or moved
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		Engine(Engine&&) = delete;
		Engine& operator=(Engine&&) = delete;

		void run();

	private:
		// Takes the enemies' attacks and heals, and walks relative to where the camera looks,
		// so W always goes up the screen, turning the player to face where it walks
		void update_player(float delta_seconds, float time_seconds) noexcept;

		// Follows the player, or flies freely after F2
		// The time is real, not the game's, so the camera keeps moving during a hit-stop
		void update_camera(float delta_seconds, float time_seconds) noexcept;
		void update_free_camera(float delta_seconds) noexcept;

		// Looks down at the player from behind, in radians like Camera
		[[nodiscard]] static Camera follow_camera(
			glm::vec2 player_position,
			float distance,
			float yaw,
			float pitch,
			glm::vec3 shake_offset) noexcept;

		// The abilities' hits freeze the game for a moment and shake the camera, so they feel heavy
		void update_impact_feedback() noexcept;

		// The thrown sword lands once it has flown, which hooks the enemies around it
		void update_void_sword(float time_seconds) noexcept;

		// Each of Whirlwind's turns hits again and reverses with an impact frame, then the player
		// lands from its leap and the seismic waves hit whoever they pass
		void update_whirlwind(float time_seconds) noexcept;

		// In pixels, how far the mouse moved while the right button turns the camera, else 0
		[[nodiscard]] glm::vec2 mouse_look_offset() noexcept;

		// Casts the abilities of the hotbar keys toward where the player faces
		void update_abilities(float time_seconds) noexcept;
		void update_selection() noexcept;

		// R restarts the level and F3 goes to the next one, and the level is cleared once the last
		// enemy dies, with the time in game seconds
		void update_level(float time_seconds) noexcept;
		void start_level(std::size_t level_index, float time_seconds) noexcept;

		// The enemies chase the player and take the hits cast this frame
		void update_enemies(float delta_seconds, float time_seconds) noexcept;

		// Space shows a damage number on every enemy without hurting it, for benchmarks
		void update_test_hits(float time_seconds) noexcept;

		void recreate_swapchain();

		// Each member uses the ones above it and is destroyed before them, so don't reorder
		Window window_ {};
		Device device_ { window_ };
		Swapchain swapchain_ { device_, window_ };
		GraphicsPipeline graphics_pipeline_ {
			device_, swapchain_.surface_format().format,
			swapchain_.depth_format(), swapchain_.sample_count()
		};
		GpuTimer gpu_timer_ { device_ };
		Gui gui_ { window_, device_, swapchain_.surface_format().format };
		DamageNumbers damage_numbers_ {
			device_, graphics_pipeline_.frame_descriptor_set_layout(),
			swapchain_.surface_format().format
		};
		HealthBars health_bars_ {
			device_, graphics_pipeline_.frame_descriptor_set_layout(),
			swapchain_.surface_format().format
		};
		EnemySimulation enemy_simulation_ { device_, damage_numbers_.buffer() };
		Renderer renderer_ {
			device_, swapchain_, graphics_pipeline_, gpu_timer_, gui_, damage_numbers_,
			health_bars_, enemy_simulation_
		};

		// An index into levels, and the game starts with the first one, like EnemySimulation
		std::size_t level_index_ {};

		Player player_ { levels[level_index_].player_start };

		// In game seconds, when the current level started and how long it took to clear it,
		// empty while enemies are left
		float level_start_time_ {};
		std::optional<float> clear_seconds_ {};

		// In world units from the player, changed with the mouse wheel
		float camera_distance_ { 25.0f };

		// Toggled with F2, to fly around the scene and to take benchmarks from a fixed spot
		bool is_camera_free_ { false };

		// In radians, turned by dragging with the right button, and yaw -45 degrees starts looking
		// along -x and -z, the diagonal from the player's start to the middle of the grid
		float follow_yaw_ { glm::radians(-45.0f) };
		float follow_pitch_ { glm::radians(-50.0f) };

		Camera camera_ {
			follow_camera(
				player_.position(),
				camera_distance_,
				follow_yaw_,
				follow_pitch_,
				glm::vec3 { 0.0f })
		};

		// In seconds, the game's clock, which stands still during a hit-stop, and how much of the
		// hit-stop is left
		float game_time_ {};
		float hit_stop_left_ {};

		// From 0 to 1, how hard the camera shakes, see update_camera
		float camera_trauma_ {};

		// In game seconds on the last frame since Whirlwind was cast, so each of its moments happens
		// once, and never cast starts as long ago
		float whirlwind_age_ { -constants::long_ago };

		// The same for the thrown sword
		float strike_age_ { -constants::long_ago };

		// Edited live from the light panel
		DirectionalLight light_ {};

		FrameTimer frame_timer_ {};
		StatsOverlay stats_overlay_ { device_, swapchain_ };

		// Real time, for the camera shake, which goes on during a hit-stop
		std::chrono::steady_clock::time_point start_time_ { std::chrono::steady_clock::now() };

		// The player, the camera and the enemies move according to the time elapsed since then
		std::chrono::steady_clock::time_point previous_frame_time_ {
			std::chrono::steady_clock::now()
		};

		// So these keys act once per press, not every frame while they're held
		bool was_camera_key_pressed_ { false };
		bool was_hit_key_pressed_ { false };
		bool was_reset_key_pressed_ { false };
		bool was_level_key_pressed_ { false };
		bool was_tab_key_pressed_ { false };
		bool was_select_button_pressed_ { false };
	};
}
