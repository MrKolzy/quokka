#include "quokka/window.hpp"

#include "quokka/constants.hpp"

#include <GLFW/glfw3.h>

#include <cstdio> // stderr
#include <print>
#include <stdexcept> // std::runtime_error

namespace quokka
{
	namespace
	{
		void glfw_error_callback(int error_code, const char* description) noexcept
		{
			std::println(stderr, "[Quokka] GLFW error ({:#x}): {}", error_code, description);
		}
	}

	Window::Window()
	{
		// Set before glfwInit so initialization errors are reported too
		glfwSetErrorCallback(&glfw_error_callback);

		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}

		// Vulkan doesn't use an OpenGL context
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, constants::is_window_resizable ? GLFW_TRUE : GLFW_FALSE);

		handle_ = glfwCreateWindow(constants::window_width, constants::window_height, constants::app_name, nullptr, nullptr);
		if (!handle_)
		{
			// The destructor doesn't run if the constructor throws, so clean up here
			glfwTerminate();

			throw std::runtime_error("Failed to create GLFW window");
		}

		// GLFW callbacks only receive the GLFWwindow*, so store this to recover the Window
		glfwSetWindowUserPointer(handle_, this);
		glfwSetFramebufferSizeCallback(handle_, &framebuffer_size_callback);
	}

	Window::~Window()
	{
		glfwDestroyWindow(handle_);
		glfwTerminate();
	}

	GLFWwindow* Window::handle() const noexcept
	{
		return handle_;
	}

	bool Window::was_resized() const noexcept
	{
		return was_resized_;
	}

	void Window::clear_resized() noexcept
	{
		was_resized_ = false;
	}

	void Window::framebuffer_size_callback(GLFWwindow* window, int /*width*/, int /*height*/) noexcept
	{
		// The engine queries the new size itself when recreating the swapchain
		auto* self { static_cast<Window*>(glfwGetWindowUserPointer(window)) };
		self->was_resized_ = true;
	}
}
