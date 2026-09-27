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

	private:
		GLFWwindow* window_ {};
	};
}
