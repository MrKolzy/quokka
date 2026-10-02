#pragma once

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

		void create_instance();
		void create_debug_messenger();
		void create_surface();
		void pick_physical_device();
		void create_logical_device();

		void create_swapchain();
		void create_image_views();

		void create_graphics_pipeline();

		void create_command_pool();
		void create_command_buffers();
		void create_frame_sync_objects();
		void create_render_finished_semaphores();

		// ***

		void recreate_swapchain();
		void cleanup_swapchain() noexcept;

		// ***

		void main_loop();
		void draw_frame();
		void record_command_buffer(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index) const;

		// ***

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from, and the window last
		Window window_ {};

		// Loads the Vulkan library and exposes functions that don't require an instance
		vk::raii::Context context_ {};
		vk::raii::Instance instance_ { nullptr };

		// Routes validation layer messages to our callback, only created in debug builds
		vk::raii::DebugUtilsMessengerEXT debug_messenger_ { nullptr };

		// Connection between Vulkan and the window
		vk::raii::SurfaceKHR surface_ { nullptr };

		// GPU selected to run the engine, implicitly destroyed with the instance
		vk::raii::PhysicalDevice physical_device_ { nullptr };

		// Family of queue_, also needed to create the command pool
		std::uint32_t queue_family_index_ {};
		// Used to create most other Vulkan objects
		vk::raii::Device device_ { nullptr };
		// Submits graphics commands and presents images, implicitly destroyed with the device
		vk::raii::Queue queue_ { nullptr };

		// Queue of images presented to the surface
		vk::raii::SwapchainKHR swapchain_ { nullptr };
		// Owned by the swapchain, implicitly destroyed with it
		std::vector<vk::Image> swapchain_images_ {};
		vk::SurfaceFormatKHR swapchain_surface_format_ {};
		vk::Extent2D swapchain_extent_ {};
		// Describe how to access each swapchain image
		std::vector<vk::raii::ImageView> swapchain_image_views_ {};

		// Descriptor set layouts and push constant ranges the shaders can access
		vk::raii::PipelineLayout pipeline_layout_ { nullptr };
		// Shader stages and fixed-function state used to draw
		vk::raii::Pipeline graphics_pipeline_ { nullptr };

		// Allocates the memory of the command buffers
		vk::raii::CommandPool command_pool_ { nullptr };
		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> command_buffers_ {};

		// Signaled by acquireNextImage, waited on by the submission, one per frame in flight
		std::vector<vk::raii::Semaphore> image_available_semaphores_ {};
		// Signaled by the submission, waited on by presentation, at least one per swapchain image
		std::vector<vk::raii::Semaphore> render_finished_semaphores_ {};
		// Signaled by the submission, waited on by the CPU, one per frame in flight
		std::vector<vk::raii::Fence> in_flight_fences_ {};

		// Frame in flight currently being recorded, cycles from 0 to max_frames_in_flight - 1
		std::uint32_t frame_index_ {};
	};
}
