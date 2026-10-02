#pragma once

#include "quokka/device.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/window.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector>

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
		void initialize_vulkan();

		void create_command_pool();
		void create_command_buffers();
		void create_frame_sync_objects();

		// ***

		void recreate_swapchain();

		// ***

		void main_loop();
		void draw_frame();
		void record_command_buffer(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index) const;

		// ***

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from, and the window last
		Window window_ {};
		Device device_ { window_ };
		Swapchain swapchain_ { device_, window_ };
		GraphicsPipeline graphics_pipeline_ { device_, swapchain_.surface_format().format };

		// Allocates the memory of the command buffers
		vk::raii::CommandPool command_pool_ { nullptr };
		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> command_buffers_ {};

		// Signaled by acquireNextImage, waited on by the submission, one per frame in flight
		std::vector<vk::raii::Semaphore> image_available_semaphores_ {};
		// Signaled by the submission, waited on by the CPU, one per frame in flight
		std::vector<vk::raii::Fence> in_flight_fences_ {};

		// Frame in flight currently being recorded, cycles from 0 to max_frames_in_flight - 1
		std::uint32_t frame_index_ {};
	};
}
