#pragma once

struct GLFWwindow;

namespace quokka
{
	// Owns the GLFW library and the application window
	class Window
	{
	public:
		Window();
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		GLFWwindow* handle() const noexcept;

		// Whether the framebuffer was resized since the last clear, so the swapchain must be recreated
		bool has_resized() const noexcept;

		void clear_resized() noexcept;

	private:
		// Called by GLFW whenever the framebuffer is resized, including when the window is minimized
		static void framebuffer_size_callback(GLFWwindow* window, int width, int height) noexcept;

		GLFWwindow* window_ {};

		// Set by the framebuffer size callback, cleared once the swapchain is recreated
		bool has_resized_ { false };
	};
}
