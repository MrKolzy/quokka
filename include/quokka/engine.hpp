#pragma once

#include "quokka/window.hpp"

// ***

#include <vulkan/vulkan_raii.hpp>

// ***

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

		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		Engine(Engine&&) = delete;
		Engine& operator=(Engine&&) = delete;

		void run();

	private:
		void initialize_vulkan();

		void create_instance();

		void setup_debug_messenger();

		void create_surface();

		void pick_physical_device();

		void create_logical_device();

		void create_swapchain();

		void create_image_views();

		void create_graphics_pipeline();

		void create_command_pool();

		void create_command_buffer();

		void create_sync_objects();

		// ***

		void main_loop();

		void draw_frame() const;

		void record_command_buffer(std::uint32_t image_index) const;

		// ***

		// Must be declared first so it's destroyed last, after every Vulkan object
		Window window_ {};

		// Loads the Vulkan library and exposes functions that don't require an instance
		vk::raii::Context context_ {};
		// Connection between the application and the Vulkan library
		vk::raii::Instance instance_ { nullptr };

		// Routes validation layer messages to our callback
		vk::raii::DebugUtilsMessengerEXT debug_messenger_ { nullptr };

		// Connection between Vulkan and the window, must be destroyed before the instance
		vk::raii::SurfaceKHR surface_ { nullptr };

		// GPU selected to run the engine, implicitly destroyed with the instance
		vk::raii::PhysicalDevice physical_device_ { nullptr };

		// Queue family of the selected GPU that executes graphics commands and presents to the surface
		std::uint32_t queue_family_index_ {};
		// Logical connection to the selected GPU, used to create most other Vulkan objects
		vk::raii::Device device_ { nullptr };
		// Submits graphics commands and presents images, implicitly destroyed with the device
		vk::raii::Queue queue_ { nullptr };

		// Queue of images presented to the surface, must be destroyed before the device
		vk::raii::SwapchainKHR swapchain_ { nullptr };
		// Images owned by the swapchain, implicitly destroyed with it
		std::vector<vk::Image> swapchain_images_ {};
		// Pixel format and color space of the swapchain images
		vk::SurfaceFormatKHR swapchain_surface_format_ {};
		// Resolution of the swapchain images in pixels
		vk::Extent2D swapchain_extent_ {};

		// Describe how to access each swapchain image, must be destroyed before the device
		std::vector<vk::raii::ImageView> swapchain_image_views_ {};

		// Uniforms and push constants available to the shaders, must be destroyed before the device
		vk::raii::PipelineLayout pipeline_layout_ { nullptr };

		// Shader stages and fixed-function state used to draw, must be destroyed before the device
		vk::raii::Pipeline graphics_pipeline_ { nullptr };

		// Allocates the memory of the command buffers, must be destroyed before the device
		vk::raii::CommandPool command_pool_ { nullptr };
		// Records the commands submitted to the queue, must be freed before the command pool
		vk::raii::CommandBuffer command_buffer_ { nullptr };

		// Signaled when the acquired swapchain image is ready to be rendered to, must be destroyed before the device
		vk::raii::Semaphore present_complete_semaphore_ { nullptr };
		// Signaled when rendering finishes, one per swapchain image, must be destroyed before the device
		std::vector<vk::raii::Semaphore> render_finished_semaphores_ {};
		// Signaled when the GPU finishes the submitted commands, so the CPU can record again, must be destroyed before the device
		vk::raii::Fence draw_fence_ { nullptr };
	};
}
