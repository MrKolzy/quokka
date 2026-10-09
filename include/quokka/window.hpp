#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <string> // std::string
#include <vector> // std::vector

struct GLFWwindow;

namespace quokka
{
	// Only the keys the engine reads, so GLFW stays hidden inside Window
	enum class Key
	{
		a,
		d,
		e,
		f2,
		five,
		four,
		one,
		q,
		r,
		s,
		space,
		t,
		three,
		two,
		w
	};

	enum class MouseButton
	{
		left,
		right
	};

	class Window
	{
	public:
		Window();
		~Window();

		// Owns the handle and GLFW stores this as the user pointer, so it can't be copied or moved
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		// Only for libraries that work with GLFW directly, such as the ImGui backend
		[[nodiscard]] GLFWwindow* handle() const noexcept;

		// In pixels, and 0x0 while minimized
		[[nodiscard]] vk::Extent2D framebuffer_extent() const noexcept;

		// Not static because it needs GLFW initialized, which a Window guarantees
		[[nodiscard]] std::vector<const char*> required_instance_extensions() const;

		// Must be destroyed before the instance
		[[nodiscard]] vk::raii::SurfaceKHR create_surface(const vk::raii::Instance& instance) const;

		[[nodiscard]] bool should_close() const noexcept;

		// Held down as of the last poll_events
		[[nodiscard]] bool is_key_pressed(Key key) const noexcept;
		[[nodiscard]] bool is_mouse_button_pressed(MouseButton button) const noexcept;

		// A captured cursor is hidden and moves without limits, so it can turn the camera
		[[nodiscard]] bool is_cursor_captured() const noexcept;
		void set_cursor_captured(bool is_captured) noexcept;

		// From 0 to 1 across the window, with 0, 0 at the top left corner
		[[nodiscard]] glm::vec2 cursor_position() const noexcept;

		// In pixels, since the previous poll_events
		[[nodiscard]] glm::vec2 cursor_offset() const noexcept;

		// In wheel notches since the previous poll_events, positive when scrolled forward
		[[nodiscard]] float scroll_offset() const noexcept;

		void set_title(const std::string& title) noexcept;

		// Run event callbacks, wait first blocks until an event arrives and poll doesn't
		void poll_events() noexcept;
		void wait_events() noexcept;

		// Set by the resize callback, cleared after recreating the swapchain
		[[nodiscard]] bool was_resized() const noexcept;
		void clear_resized() noexcept;

	private:
		static void framebuffer_size_callback(GLFWwindow* window, int width, int height) noexcept;
		static void scroll_callback(GLFWwindow* window, double x_offset, double y_offset) noexcept;

		GLFWwindow* handle_ { nullptr };
		bool was_resized_ { false };

		glm::dvec2 previous_cursor_position_ {};
		glm::vec2 cursor_offset_ {};

		// Added up by the scroll callback, which can run several times per poll
		float scroll_offset_ {};
	};
}
