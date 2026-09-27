#pragma once

#include "quokka/window.hpp"

// ***

#include <vulkan/vulkan_raii.hpp>

// ***

#include <cstdint> // std::uint32_t

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

		void pick_physical_device();

		void create_logical_device();

		// ***

		void main_loop();

		// ***

		// Must be declared first so it's destroyed last, after every Vulkan object
		Window window_ {};

		// Loads the Vulkan library and exposes functions that don't require an instance
		vk::raii::Context context_ {};
		// Connection between the application and the Vulkan library
		vk::raii::Instance instance_ { nullptr };

		// Routes validation layer messages to our callback
		vk::raii::DebugUtilsMessengerEXT debug_messenger_ { nullptr };

		// GPU selected to run the engine, implicitly destroyed with the instance
		vk::raii::PhysicalDevice physical_device_ { nullptr };

		// Queue family of the selected GPU that executes graphics commands
		std::uint32_t graphics_queue_family_index_ {};
		// Logical connection to the selected GPU, used to create most other Vulkan objects
		vk::raii::Device device_ { nullptr };
		// Submits graphics commands to the GPU, implicitly destroyed with the device
		vk::raii::Queue graphics_queue_ { nullptr };
	};
}
