#pragma once

#include "quokka/device.hpp"
#include "quokka/frame_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/particle_system.hpp"
#include "quokka/renderer.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/window.hpp"

namespace quokka
{
	class Engine
	{
	public:
		Engine() = default;
		~Engine() = default;

		// Not copyable or movable: members reference each other
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		Engine(Engine&&) = delete;
		Engine& operator=(Engine&&) = delete;

		void run();

	private:
		void recreate_swapchain();

		// Order matters: each member uses the ones above it, and they're destroyed in reverse
		Window window_ {};
		Device device_ { window_ };
		Swapchain swapchain_ { device_, window_ };
		GraphicsPipeline graphics_pipeline_ {
			device_, swapchain_.surface_format().format,
			swapchain_.depth_format(), swapchain_.sample_count()
		};
		ParticleSystem particle_system_ {
			device_, graphics_pipeline_, swapchain_.surface_format().format,
			swapchain_.depth_format(), swapchain_.sample_count()
		};
		Renderer renderer_ { device_, swapchain_, graphics_pipeline_, particle_system_ };

		FrameTimer frame_timer_ {};
	};
}
