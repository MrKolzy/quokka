#include "quokka/window.hpp"

#include "quokka/constants.hpp"

#include <GLFW/glfw3.h>

#include <cstdint> // std::uint32_t
#include <cstdio> // stderr
#include <print> // std::println
#include <stdexcept> // std::runtime_error
#include <utility> // std::unreachable

namespace quokka
{
	namespace
	{
		void glfw_error_callback(int error_code, const char* description) noexcept
		{
			std::println(stderr, "[Quokka] GLFW error ({:#x}): {}", error_code, description);
		}

		[[nodiscard]] constexpr int glfw_key(Key key) noexcept
		{
			switch (key)
			{
			case Key::a:
				return GLFW_KEY_A;
			case Key::d:
				return GLFW_KEY_D;
			case Key::e:
				return GLFW_KEY_E;
			case Key::q:
				return GLFW_KEY_Q;
			case Key::s:
				return GLFW_KEY_S;
			case Key::w:
				return GLFW_KEY_W;
			}

			// Every key is handled above
			std::unreachable();
		}

		[[nodiscard]] glm::dvec2 cursor_position(GLFWwindow* window) noexcept
		{
			glm::dvec2 position {};
			glfwGetCursorPos(window, &position.x, &position.y);

			return position;
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

		handle_ = glfwCreateWindow(constants::window_width, constants::window_height,
			constants::app_name, nullptr, nullptr);
		if (!handle_)
		{
			// The destructor doesn't run if the constructor throws
			glfwTerminate();

			throw std::runtime_error("Failed to create GLFW window");
		}

		// Lets the static callback recover the Window from the GLFW window handle
		glfwSetWindowUserPointer(handle_, this);
		glfwSetFramebufferSizeCallback(handle_, &framebuffer_size_callback);

		// Only applies while the cursor is captured, and skips the pointer acceleration of the OS
		if (glfwRawMouseMotionSupported())
		{
			glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		}
	}

	Window::~Window()
	{
		glfwDestroyWindow(handle_);
		glfwTerminate();
	}

	vk::Extent2D Window::framebuffer_extent() const noexcept
	{
		int width {};
		int height {};
		glfwGetFramebufferSize(handle_, &width, &height);

		return {
			.width { static_cast<std::uint32_t>(width) },
			.height { static_cast<std::uint32_t>(height) }
		};
	}

	std::vector<const char*> Window::required_instance_extensions() const
	{
		std::uint32_t glfw_extension_count {};
		const auto glfw_extensions { glfwGetRequiredInstanceExtensions(&glfw_extension_count) };
		if (!glfw_extensions)
		{
			throw std::runtime_error("Failed to get the instance extensions required by GLFW");
		}

		// Parentheses because braces could pick the initializer_list constructor
		return std::vector<const char*>(glfw_extensions, glfw_extensions + glfw_extension_count);
	}

	vk::raii::SurfaceKHR Window::create_surface(const vk::raii::Instance& instance) const
	{
		VkSurfaceKHR raw_surface {};

		// GLFW hides the surface creation of each platform, such as Win32, X11 or Wayland
		if (glfwCreateWindowSurface(*instance, handle_, nullptr, &raw_surface) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create window surface");
		}

		// Wrap it so it's destroyed automatically
		return vk::raii::SurfaceKHR { instance, raw_surface };
	}

	bool Window::should_close() const noexcept
	{
		return glfwWindowShouldClose(handle_);
	}

	bool Window::is_key_pressed(Key key) const noexcept
	{
		return glfwGetKey(handle_, glfw_key(key)) == GLFW_PRESS;
	}

	bool Window::is_right_mouse_button_pressed() const noexcept
	{
		return glfwGetMouseButton(handle_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	}

	bool Window::is_cursor_captured() const noexcept
	{
		return glfwGetInputMode(handle_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
	}

	void Window::set_cursor_captured(bool is_captured) noexcept
	{
		glfwSetInputMode(
			handle_,
			GLFW_CURSOR,
			is_captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
		);

		// Changing the mode moves the cursor, which must not count as an offset
		previous_cursor_position_ = cursor_position(handle_);
	}

	glm::vec2 Window::cursor_offset() const noexcept
	{
		return cursor_offset_;
	}

	void Window::set_title(const std::string& title) noexcept
	{
		glfwSetWindowTitle(handle_, title.c_str());
	}

	void Window::poll_events() noexcept
	{
		glfwPollEvents();

		const glm::dvec2 position { cursor_position(handle_) };
		cursor_offset_ = glm::vec2 { position - previous_cursor_position_ };
		previous_cursor_position_ = position;
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

	void Window::framebuffer_size_callback(GLFWwindow* window, int, int) noexcept
	{
		// The engine queries the new size itself when recreating the swapchain
		auto* self { static_cast<Window*>(glfwGetWindowUserPointer(window)) };
		self->was_resized_ = true;
	}
}
