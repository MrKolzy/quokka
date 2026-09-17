#pragma once

#include <vulkan/vulkan_raii.hpp>

// Forward declaration
struct GLFWwindow;

namespace quokka
{
	class Engine
	{
	public:
		Engine() = default;
		~Engine();

		// Disable copy constructor and copy assignment operator
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		// Disable move constructor and move assignment operator
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

		// Provides access to Vulkan before creating an instance
		vk::raii::Context context_ {};
		// Connection between the application and the Vulkan library
		vk::raii::Instance instance_ { nullptr };
	};
}
