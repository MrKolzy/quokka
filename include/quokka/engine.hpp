#pragma once

#include "quokka/camera.hpp"
#include "quokka/damage_numbers.hpp"
#include "quokka/device.hpp"
#include "quokka/enemy_simulation.hpp"
#include "quokka/frame_timer.hpp"
#include "quokka/gpu_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/gui.hpp"
#include "quokka/health_bars.hpp"
#include "quokka/light.hpp"
#include "quokka/player.hpp"
#include "quokka/renderer.hpp"
#include "quokka/stats_overlay.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/window.hpp"

#include <glm/glm.hpp>

#include <chrono> // std::chrono::steady_clock

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
		// so W always goes up the screen
		void update_player(float delta_seconds, float time_seconds) noexcept;

		// Follows the player, or flies freely after F2
		void update_camera(float delta_seconds) noexcept;
		void update_free_camera(float delta_seconds) noexcept;

		// Looks down at the player from a fixed angle, like in Diablo or Path of Exile
		[[nodiscard]] static Camera follow_camera(
			glm::vec2 player_position,
			float distance) noexcept;

		// The player aims where the cursor points on the ground, so it must run after the camera
		void update_aim() noexcept;

		// Casts the abilities of the hotbar keys and of their mouse buttons toward the cursor,
		// so it must run after the aim
		void update_abilities(float time_seconds) noexcept;

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

		Player player_ { EnemySimulation::player_start() };

		// In world units from the player, changed with the mouse wheel
		float camera_distance_ { 25.0f };

		// Toggled with F2, to fly around the scene and to take benchmarks from a fixed spot
		bool is_camera_free_ { false };

		Camera camera_ { follow_camera(player_.position(), camera_distance_) };

		// Edited live from the light panel
		DirectionalLight light_ {};

		FrameTimer frame_timer_ {};
		StatsOverlay stats_overlay_ { device_, swapchain_ };

		// The animations computed on the GPU, such as damage numbers, are timed from here
		std::chrono::steady_clock::time_point start_time_ { std::chrono::steady_clock::now() };

		// The player, the camera and the enemies move according to the time elapsed since then
		std::chrono::steady_clock::time_point previous_frame_time_ {
			std::chrono::steady_clock::now()
		};

		// So these keys act once per press, not every frame while they're held
		bool was_camera_key_pressed_ { false };
		bool was_hit_key_pressed_ { false };
		bool was_reset_key_pressed_ { false };
	};
}
