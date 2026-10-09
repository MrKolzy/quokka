#pragma once

#include "quokka/camera.hpp"
#include "quokka/device.hpp"
#include "quokka/frame_timer.hpp"
#include "quokka/gpu_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/gui.hpp"
#include "quokka/light.hpp"
#include "quokka/renderer.hpp"
#include "quokka/stats_overlay.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/window.hpp"

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
		void update_camera(float delta_seconds) noexcept;
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
		Renderer renderer_ { device_, swapchain_, graphics_pipeline_, gpu_timer_, gui_ };

		Camera camera_ { Renderer::initial_camera() };

		// Edited live from the light panel
		DirectionalLight light_ {};

		FrameTimer frame_timer_ {};
		StatsOverlay stats_overlay_ { device_, swapchain_ };

		// The camera moves according to the time elapsed since then
		std::chrono::steady_clock::time_point previous_frame_time_ {
			std::chrono::steady_clock::now()
		};
	};
}
