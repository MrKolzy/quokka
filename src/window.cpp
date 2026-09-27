#include "quokka/window.hpp"

// ***

#include "quokka/constants.hpp"

// ***

#include <GLFW/glfw3.h>

// ***

#include <cstdio> // stderr
#include <print>
#include <stdexcept> // std::runtime_error

namespace quokka
{
	// Helpers only visible inside this file
	namespace
	{
		// Called by GLFW whenever one of its functions fails
		void glfw_error_callback(int error_code, const char* description) noexcept
		{
			std::println(stderr, "[Quokka] GLFW error ({:#x}): {}", error_code, description);
		}
	}
}

namespace quokka
{
	Window::Window()
	{
		// Report the reason behind any GLFW failure, it can be set before initialization
		glfwSetErrorCallback(&glfw_error_callback);

		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}

		// Disable OpenGL context creation, as Vulkan doesn't need one
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// Disable resizing, as it requires recreating the swapchain
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window_ = glfwCreateWindow(constants::width, constants::height, constants::name, nullptr, nullptr);

		if (!window_)
		{
			// The destructor doesn't run if the constructor throws, so clean up here
			glfwTerminate();

			throw std::runtime_error("Failed to create GLFW window");
		}
	}

	Window::~Window()
	{
		glfwDestroyWindow(window_);
		glfwTerminate();
	}

	GLFWwindow* Window::handle() const noexcept
	{
		return window_;
	}
}
