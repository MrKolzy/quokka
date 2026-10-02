#pragma once

#include "quokka/device.hpp"
#include "quokka/frame_timer.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/renderer.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/window.hpp"

namespace quokka
{
	// Owns the window and every Vulkan object, and runs the main loop
	class Engine
	{
	public:
		Engine() = default;
		~Engine() = default;

		// Neither copyable nor movable: owns the Window, which isn't either
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		Engine(Engine&&) = delete;
		Engine& operator=(Engine&&) = delete;

		void run();

	private:
		void recreate_swapchain();

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from, and the window last
		Window window_ {};
		Device device_ { window_ };
		Swapchain swapchain_ { device_, window_ };
		GraphicsPipeline graphics_pipeline_ { device_, swapchain_.surface_format().format };
		Renderer renderer_ { device_, swapchain_, graphics_pipeline_ };

		FrameTimer frame_timer_ {};
	};
}
