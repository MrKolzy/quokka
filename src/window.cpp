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

		// Resizing is allowed, the engine recreates the swapchain to match the new size
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		window_ = glfwCreateWindow(constants::window_width, constants::window_height, constants::app_name, nullptr, nullptr);

		if (!window_)
		{
			// The destructor doesn't run if the constructor throws, so clean up here
			glfwTerminate();

			throw std::runtime_error("Failed to create GLFW window");
		}

		// GLFW is a C library and only passes the window handle, so store this object to find it from the callback
		glfwSetWindowUserPointer(window_, this);
		glfwSetFramebufferSizeCallback(window_, &framebuffer_size_callback);
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

	bool Window::has_resized() const noexcept
	{
		return has_resized_;
	}

	void Window::clear_resized() noexcept
	{
		has_resized_ = false;
	}

	void Window::framebuffer_size_callback(GLFWwindow* window, int /*width*/, int /*height*/) noexcept
	{
		// The engine queries the new size itself when recreating the swapchain
		auto* self { static_cast<Window*>(glfwGetWindowUserPointer(window)) };
		self->has_resized_ = true;
	}
}
