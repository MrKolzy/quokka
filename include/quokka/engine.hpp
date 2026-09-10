#pragma once

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
		void initialize_vulkan();
		void main_loop();

		GLFWwindow* window_ {};
	};
}
