#include "quokka/window.hpp"

#include "quokka/constants.hpp"

#include <GLFW/glfw3.h>

#include <cstdint> // std::uint32_t
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

	vk::Extent2D Window::framebuffer_extent() const noexcept
	{
		// Framebuffer size is in pixels, unlike glfwGetWindowSize, which returns screen coordinates
		int width {};
		int height {};
		glfwGetFramebufferSize(handle_, &width, &height);

		return { .width { static_cast<std::uint32_t>(width) }, .height { static_cast<std::uint32_t>(height) } };
	}

	std::vector<const char*> Window::required_instance_extensions() const
	{
		std::uint32_t glfw_extension_count {};
		const auto glfw_extensions { glfwGetRequiredInstanceExtensions(&glfw_extension_count) };
		if (!glfw_extensions)
		{
			throw std::runtime_error("Failed to get the Vulkan instance extensions required by GLFW");
		}

		// Parentheses, not braces: this is the iterator-range constructor
		std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

		return extensions;
	}

	vk::raii::SurfaceKHR Window::create_surface(const vk::raii::Instance& instance) const
	{
		// Raw C handle, since GLFW is a C library and doesn't know about vk::raii
		VkSurfaceKHR raw_surface {};

		// GLFW hides the platform-specific surface creation (Win32, X11, Wayland, ...)
		if (glfwCreateWindowSurface(*instance, handle_, nullptr, &raw_surface) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create window surface");
		}

		// Take ownership of the raw handle so it's destroyed automatically
		return vk::raii::SurfaceKHR { instance, raw_surface };
	}

	bool Window::should_close() const noexcept
	{
		return glfwWindowShouldClose(handle_);
	}

	void Window::set_title(const std::string& title) noexcept
	{
		glfwSetWindowTitle(handle_, title.c_str());
	}

	void Window::poll_events() noexcept
	{
		glfwPollEvents();
	}

	void Window::wait_events() noexcept
	{
		glfwWaitEvents();
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
