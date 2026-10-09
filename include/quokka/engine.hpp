#pragma once

#include "quokka/camera.hpp"
#include "quokka/damage_numbers.hpp"
#include "quokka/device.hpp"
#include "quokka/enemy_simulation.hpp"
#include "quokka/frame_timer.hpp"
#include "quokka/gpu_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/gui.hpp"
#include "quokka/light.hpp"
#include "quokka/renderer.hpp"
#include "quokka/stats_overlay.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/window.hpp"

#include <glm/glm.hpp>

#include <chrono> // std::chrono::steady_clock
#include <random> // std::mt19937

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
		void update_camera(float delta_seconds) noexcept;

		// Until there's a player, the enemies chase the point of the ground the camera looks at
		void update_enemies(float delta_seconds) noexcept;

		// A test until there's combat, every enemy takes a hit with its damage number
		void update_test_hits(float time_seconds);

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
			device_, graphics_pipeline_.descriptor_set_layout(), swapchain_.surface_format().format
		};
		EnemySimulation enemy_simulation_ { device_ };
		Renderer renderer_ {
			device_, swapchain_, graphics_pipeline_, gpu_timer_, gui_, damage_numbers_,
			enemy_simulation_
		};

		Camera camera_ { EnemySimulation::initial_camera() };

		// Edited live from the light panel
		DirectionalLight light_ {};

		FrameTimer frame_timer_ {};
		StatsOverlay stats_overlay_ { device_, swapchain_ };

		// The animations computed on the GPU, such as damage numbers, are timed from here
		std::chrono::steady_clock::time_point start_time_ { std::chrono::steady_clock::now() };

		// The camera moves according to the time elapsed since then
		std::chrono::steady_clock::time_point previous_frame_time_ {
			std::chrono::steady_clock::now()
		};

		// Default seed, so every run deals the same damage, which keeps benchmarks repeatable
		std::mt19937 random_engine_ {};

		// On the ground as world x and z, kept while the camera looks above the horizon
		glm::vec2 chase_target_ {};

		// So these keys act once per press, not every frame while they're held
		bool was_hit_key_pressed_ { false };
		bool was_reset_key_pressed_ { false };
	};
}
