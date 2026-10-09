#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace quokka
{
	class Device;
	class Window;

	// The swapchain is sRGB and encodes what ImGui draws, so colors picked in sRGB, like
	// ImGui's style, must be converted to linear first or they'd come out washed out
	[[nodiscard]] float srgb_to_linear(float value) noexcept;

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

		// The debug panels, such as the stats and the light, toggled with F1 and hidden at first
		// The game's own interface, such as the hotbar, is always shown
		[[nodiscard]] bool are_panels_visible() const noexcept;

		// True while the interface uses the input, so the camera must ignore it
		[[nodiscard]] bool wants_mouse() const noexcept;
		[[nodiscard]] bool wants_keyboard() const noexcept;

	private:
		const Window& window_;
		bool are_panels_visible_ { false };
	};
}
