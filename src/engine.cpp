#include "quokka/engine.hpp"

// ***

#include "quokka/constants.hpp"

// ***

#include <GLFW/glfw3.h>

// ***

#include <stdexcept>

namespace quokka
{
	Engine::~Engine()
	{
		if (window_)
		{
			glfwDestroyWindow(window_);
		}

		glfwTerminate();
	}

	void Engine::run()
	{
		initialize_window();
		initialize_vulkan();
		main_loop();
	}

	void Engine::initialize_window()
	{
		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}

		// Disable OpenGL context creation
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// Disable window resizing for now
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window_ = glfwCreateWindow(constants::width, constants::height, "Quokka", nullptr, nullptr);

		if (!window_)
		{
			throw std::runtime_error("Failed to create GLFW window");
		}
	}

	void Engine::initialize_vulkan()
	{
	}

	void Engine::main_loop()
	{
		while (!glfwWindowShouldClose(window_))
		{
			glfwPollEvents();
		}
	}
}
