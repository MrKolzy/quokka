#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <string>
#include <vector>

struct GLFWwindow;

namespace quokka
{
	// Owns the GLFW library and the application window
	class Window
	{
	public:
		Window();
		~Window();

		// Not copyable: would destroy the window twice
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		// Not movable: GLFW stores this as the user pointer
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		// In pixels, and 0x0 while minimized
		[[nodiscard]] vk::Extent2D framebuffer_extent() const noexcept;

		// Not static: GLFW must be initialized first, which an existing Window guarantees
		[[nodiscard]] std::vector<const char*> required_instance_extensions() const;

		// Owned by the caller, and must be destroyed before the instance
		[[nodiscard]] vk::raii::SurfaceKHR create_surface(const vk::raii::Instance& instance) const;

		[[nodiscard]] bool should_close() const noexcept;

		void set_title(const std::string& title) noexcept;

		// Process pending events and run their callbacks, poll returns immediately and wait blocks until there's one
		void poll_events() noexcept;
		void wait_events() noexcept;

		// Set when the framebuffer is resized; the engine recreates the swapchain and calls clear_resized()
		[[nodiscard]] bool was_resized() const noexcept;
		void clear_resized() noexcept;

	private:
		// Also called when the window is minimized (the size becomes 0x0)
		static void framebuffer_size_callback(GLFWwindow* window, int width, int height) noexcept;

		GLFWwindow* handle_ { nullptr };
		bool was_resized_ { false };
	};
}
