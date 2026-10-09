#include "quokka/gui.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/window.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <algorithm> // std::max, std::min
#include <cmath> // std::pow
#include <stdexcept> // std::runtime_error

namespace quokka
{
	namespace
	{
		// Shows and hides the debug panels, like the debug overlays of many games
		constexpr ImGuiKey panels_key { ImGuiKey_F1 };

		// Shrinks each window that's bigger than the viewport and moves it back inside,
		// so none is left cut off at the edges
		void keep_windows_inside(const ImGuiViewport& viewport) noexcept
		{
			// Popups, menus and tooltips place themselves, and children follow their parent
			constexpr ImGuiWindowFlags self_placed_flags {
				ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup |
				ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_ChildMenu
			};

			for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
			{
				if (window->Flags & self_placed_flags)
				{
					continue;
				}

				// Not when collapsed or when it sizes itself to fit its contents
				const bool can_shrink {
					!window->Collapsed && !(window->Flags & ImGuiWindowFlags_AlwaysAutoResize)
				};
				const ImVec2 size {
					can_shrink ? std::min(window->Size.x, viewport.WorkSize.x) : window->Size.x,
					can_shrink ? std::min(window->Size.y, viewport.WorkSize.y) : window->Size.y
				};

				// Applied by its next Begin, so the position below uses the new size directly
				if (size.x != window->Size.x || size.y != window->Size.y)
				{
					ImGui::SetWindowSize(window, size);
				}

				const ImVec2 last_position {
					viewport.WorkPos.x + viewport.WorkSize.x - size.x,
					viewport.WorkPos.y + viewport.WorkSize.y - size.y
				};

				// Not std::clamp, because a window that can't shrink may still be bigger,
				// which puts its maximum below its minimum, and then the minimum wins
				const ImVec2 position {
					std::max(viewport.WorkPos.x, std::min(window->Pos.x, last_position.x)),
					std::max(viewport.WorkPos.y, std::min(window->Pos.y, last_position.y))
				};

				if (position.x != window->Pos.x || position.y != window->Pos.y)
				{
					ImGui::SetWindowPos(window, position);
				}
			}
		}
	}

	// ***

	float srgb_to_linear(float value) noexcept
	{
		// The exact sRGB curve, which is linear near black and a 2.4 power above it
		return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
	}

	// ImGui cycles through one copy of its vertex buffers per frame, and needs at least 2
	static_assert(constants::max_frames_in_flight >= 2);

	Gui::Gui(const Window& window, const Device& device, vk::Format color_format)
		: window_ { window }
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		// Window positions aren't saved to an imgui.ini file, so every run starts the same
		ImGui::GetIO().IniFilename = nullptr;

		// The style colors are sRGB, and the sRGB swapchain would encode them a second time,
		// so they're converted to linear, which is what it expects
		for (ImVec4& color : ImGui::GetStyle().Colors)
		{
			color.x = srgb_to_linear(color.x);
			color.y = srgb_to_linear(color.y);
			color.z = srgb_to_linear(color.z);
		}

		// Chains to the callbacks Window already set, such as the resize one
		if (!ImGui_ImplGlfw_InitForVulkan(window.handle(), true))
		{
			// The destructor doesn't run if the constructor throws
			ImGui::DestroyContext();

			throw std::runtime_error("Failed to initialize the GLFW backend of ImGui");
		}

		// Drawn in its own rendering, straight into the swapchain image after the MSAA resolve
		const vk::PipelineRenderingCreateInfo rendering_info {
			.colorAttachmentCount { 1 },
			.pColorAttachmentFormats { &color_format }
		};

		ImGui_ImplVulkan_InitInfo init_info {
			// Same version the instance was created with
			.ApiVersion { vk::ApiVersion13 },
			.Instance { *device.instance() },
			.PhysicalDevice { *device.physical_device() },
			.Device { *device.logical_device() },
			.QueueFamily { device.queue_family_index() },
			.Queue { *device.queue() },
			// Lets the backend create its own pool, with what the font texture needs
			.DescriptorPoolSize { IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
			.MinImageCount { constants::max_frames_in_flight },
			.ImageCount { constants::max_frames_in_flight },
			.PipelineInfoMain {
				.MSAASamples { VK_SAMPLE_COUNT_1_BIT },
				// To the C struct, or the braces would start filling its first member
				.PipelineRenderingCreateInfo {
					static_cast<VkPipelineRenderingCreateInfo>(rendering_info)
				}
			},
			.UseDynamicRendering { true }
		};

		if (!ImGui_ImplVulkan_Init(&init_info))
		{
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();

			throw std::runtime_error("Failed to initialize the Vulkan backend of ImGui");
		}
	}

	Gui::~Gui()
	{
		// In reverse order of initialization
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void Gui::begin_frame() noexcept
	{
		// While the camera captures the cursor, its hidden moves must not hover or drag windows
		ImGuiIO& io { ImGui::GetIO() };
		if (window_.is_cursor_captured())
		{
			io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		}
		else
		{
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (ImGui::IsKeyPressed(panels_key, false))
		{
			are_panels_visible_ = !are_panels_visible_;
		}

		keep_windows_inside(*ImGui::GetMainViewport());
	}

	void Gui::end_frame() noexcept
	{
		ImGui::Render();
	}

	void Gui::record(const vk::raii::CommandBuffer& command_buffer) const noexcept
	{
		// Also uploads the font texture the first time, with a submission of its own
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
	}

	bool Gui::are_panels_visible() const noexcept
	{
		return are_panels_visible_;
	}

	bool Gui::wants_mouse() const noexcept
	{
		return ImGui::GetIO().WantCaptureMouse;
	}

	bool Gui::wants_keyboard() const noexcept
	{
		return ImGui::GetIO().WantCaptureKeyboard;
	}
}
