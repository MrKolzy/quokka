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

		// True after a framebuffer resize until clear_resized(), so the swapchain must be recreated
		bool has_resized() const noexcept;
		void clear_resized() noexcept;

	private:
		// Called by GLFW whenever the framebuffer is resized, including when the window is minimized
		static void framebuffer_size_callback(GLFWwindow* window, int width, int height) noexcept;

		GLFWwindow* handle_ {};
		bool has_resized_ { false };
	};
}
