#pragma once

#include <vulkan/vulkan_raii.hpp>

// ***

struct GLFWwindow;

namespace quokka
{
	class Engine
	{
	public:
		Engine() = default;
		~Engine();

		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		Engine(Engine&&) = delete;
		Engine& operator=(Engine&&) = delete;

		void run();

	private:
		void initialize_window();

		// ***

		void initialize_vulkan();

		void create_instance();

		// ***

		void main_loop();

		// ***

		GLFWwindow* window_ {};

		// Loads the Vulkan library and exposes functions that don't require an instance
		vk::raii::Context context_ {};
		// Connection between the application and the Vulkan library
		vk::raii::Instance instance_ { nullptr };
	};
}
