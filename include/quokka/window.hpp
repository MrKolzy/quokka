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

		// Not copyable: would destroy the window twice
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		// Not movable: GLFW stores this as the user pointer
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		[[nodiscard]] GLFWwindow* handle() const noexcept;

		// Set when the framebuffer is resized; the engine recreates the swapchain and calls clear_resized()
		[[nodiscard]] bool has_resized() const noexcept;
		void clear_resized() noexcept;

	private:
		// Called by GLFW whenever the framebuffer is resized, including when the window is minimized
		static void framebuffer_size_callback(GLFWwindow* window, int width, int height) noexcept;

		GLFWwindow* handle_ { nullptr };
		bool has_resized_ { false };
	};
}
