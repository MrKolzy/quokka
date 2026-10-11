#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	class Device;
	class Window;

	class Gui
	{
	public:
		Gui(const Window& window, const Device& device, vk::Format color_format);
		~Gui();

		// ImGui state is global, so there's only one, and it can't be copied or moved
		Gui(const Gui&) = delete;
		Gui& operator=(const Gui&) = delete;
		Gui(Gui&&) = delete;
		Gui& operator=(Gui&&) = delete;

		// Every ImGui call between the two adds to this frame's interface
		void begin_frame() noexcept;
		void end_frame() noexcept;

		// Inside a rendering with one sample and no depth, after end_frame
		void record(const vk::raii::CommandBuffer& command_buffer) const noexcept;

		// Toggled with F1, and while hidden none of the interface is recorded
		[[nodiscard]] bool is_visible() const noexcept;

		// True while the interface uses the input, so the camera must ignore it
		[[nodiscard]] bool wants_mouse() const noexcept;
		[[nodiscard]] bool wants_keyboard() const noexcept;

	private:
		const Window& window_;
		bool is_visible_ { true };
	};
}
