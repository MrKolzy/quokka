#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <string> // std::string
#include <vector> // std::vector

struct GLFWwindow;

namespace quokka
{
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

		// In pixels, and 0x0 while minimized
		[[nodiscard]] vk::Extent2D framebuffer_extent() const noexcept;

		// Not static because it needs GLFW initialized, which a Window guarantees
		[[nodiscard]] std::vector<const char*> required_instance_extensions() const;

		// Must be destroyed before the instance
		[[nodiscard]] vk::raii::SurfaceKHR create_surface(const vk::raii::Instance& instance) const;

		[[nodiscard]] bool should_close() const noexcept;

		void set_title(const std::string& title) noexcept;

		// Run event callbacks, waiting for an event first (wait) or not (poll)
		void poll_events() noexcept;
		void wait_events() noexcept;

		// Set by the resize callback, cleared after recreating the swapchain
		[[nodiscard]] bool was_resized() const noexcept;
		void clear_resized() noexcept;

	private:
		static void framebuffer_size_callback(GLFWwindow* window, int width, int height) noexcept;

		GLFWwindow* handle_ { nullptr };
		bool was_resized_ { false };
	};
}
