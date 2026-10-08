#pragma once

#include "quokka/image.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector> // std::vector

namespace quokka
{
	class Device;
	class Window;

	class Swapchain
	{
	public:
		Swapchain(const Device& device, const Window& window);
		~Swapchain() = default;

		// Other modules keep references to it, so it can't be copied or moved
		Swapchain(const Swapchain&) = delete;
		Swapchain& operator=(const Swapchain&) = delete;
		Swapchain(Swapchain&&) = delete;
		Swapchain& operator=(Swapchain&&) = delete;

		// Destroys the current images right away, so wait until the GPU is idle first
		void recreate();

		[[nodiscard]] const vk::raii::SwapchainKHR& handle() const noexcept;
		[[nodiscard]] const vk::SurfaceFormatKHR& surface_format() const noexcept;
		[[nodiscard]] vk::Extent2D extent() const noexcept;
		[[nodiscard]] vk::SampleCountFlagBits sample_count() const noexcept;
		[[nodiscard]] vk::Image image(std::uint32_t index) const noexcept;
		[[nodiscard]] const vk::raii::ImageView& image_view(std::uint32_t index) const noexcept;
		[[nodiscard]] vk::Image msaa_color_image() const noexcept;
		[[nodiscard]] const vk::raii::ImageView& msaa_color_image_view() const noexcept;
		[[nodiscard]] vk::Format depth_format() const noexcept;
		[[nodiscard]] vk::Image depth_image() const noexcept;
		[[nodiscard]] const vk::raii::ImageView& depth_image_view() const noexcept;
		[[nodiscard]] const vk::raii::Semaphore& render_finished_semaphore(
			std::uint32_t index) const noexcept;

	private:
		void create_swapchain();
		void create_image_views();
		void create_msaa_color_resources();
		void create_depth_resources();
		void create_render_finished_semaphores();
		void cleanup_swapchain() noexcept;

		const Device& device_;
		const Window& window_;

		// Only depend on the GPU, so recreate keeps them
		vk::Format depth_format_;
		vk::SampleCountFlagBits sample_count_;

		vk::SurfaceFormatKHR surface_format_ {};
		vk::Extent2D extent_ {};

		// Each one is destroyed before the object it came from, so don't reorder
		vk::raii::SwapchainKHR swapchain_ { nullptr };

		// Owned and destroyed by the swapchain
		std::vector<vk::Image> images_ {};

		std::vector<vk::raii::ImageView> image_views_ {};

		// Shared by every frame in flight, because the GPU draws them one at a time
		// The MSAA ones stay null when sample_count_ is 1
		Image msaa_color_image_ {};
		vk::raii::ImageView msaa_color_image_view_ { nullptr };
		Image depth_image_ {};
		vk::raii::ImageView depth_image_view_ { nullptr };

		// Waited on by presentation, one per swapchain image
		std::vector<vk::raii::Semaphore> render_finished_semaphores_ {};
	};
}
