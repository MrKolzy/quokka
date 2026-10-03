#pragma once

#include "quokka/device.hpp"
#include "quokka/image.hpp"
#include "quokka/window.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector>

namespace quokka
{
	// Owns the images presented to the window and everything that depends on their number or size
	class Swapchain
	{
	public:
		Swapchain(const Device& device, const Window& window);
		~Swapchain() = default;

		// Neither copyable nor movable: it's recreated in place with recreate(), never replaced
		Swapchain(const Swapchain&) = delete;
		Swapchain& operator=(const Swapchain&) = delete;
		Swapchain(Swapchain&&) = delete;
		Swapchain& operator=(Swapchain&&) = delete;

		// Destroys the current swapchain and creates a new one for the current window size
		void recreate();

		[[nodiscard]] const vk::raii::SwapchainKHR& handle() const noexcept;
		[[nodiscard]] const vk::SurfaceFormatKHR& surface_format() const noexcept;
		[[nodiscard]] vk::Extent2D extent() const noexcept;
		[[nodiscard]] vk::Image image(std::uint32_t index) const noexcept;
		[[nodiscard]] const vk::raii::ImageView& image_view(std::uint32_t index) const noexcept;
		[[nodiscard]] vk::Format depth_format() const noexcept;
		[[nodiscard]] vk::Image depth_image() const noexcept;
		[[nodiscard]] const vk::raii::ImageView& depth_image_view() const noexcept;
		[[nodiscard]] const vk::raii::Semaphore& render_finished_semaphore(std::uint32_t index) const noexcept;

	private:
		void create_swapchain();
		void create_image_views();
		void create_depth_resources();
		void create_render_finished_semaphores();
		void cleanup_swapchain() noexcept;

		const Device& device_;
		const Window& window_;
		// Chosen once in the constructor, since it only depends on the GPU
		vk::Format depth_format_;

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from
		// Queue of images presented to the surface
		vk::raii::SwapchainKHR swapchain_ { nullptr };
		// Owned by the swapchain, implicitly destroyed with it
		std::vector<vk::Image> images_ {};
		vk::SurfaceFormatKHR surface_format_ {};
		vk::Extent2D extent_ {};
		// Describe how to access each swapchain image
		std::vector<vk::raii::ImageView> image_views_ {};
		// Depth of the closest fragment drawn so far at each pixel, shared by every frame in flight
		Image depth_image_ {};
		vk::raii::ImageView depth_image_view_ { nullptr };

		// Signaled by the submission, waited on by presentation, at least one per swapchain image
		std::vector<vk::raii::Semaphore> render_finished_semaphores_ {};
	};
}
