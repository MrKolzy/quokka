#include "quokka/swapchain.hpp"

#include "quokka/constants.hpp"
#include "quokka/image.hpp"

#include <algorithm> // std::clamp, std::max, std::ranges
#include <cstdint> // std::uint32_t
#include <limits>
#include <print>
#include <vector>

namespace quokka
{
	namespace
	{
		// Prefers 8-bit BGRA sRGB, so the hardware gamma-corrects the shader output on write,
		// otherwise falls back to the first available format
		vk::SurfaceFormatKHR choose_swapchain_surface_format(const std::vector<vk::SurfaceFormatKHR>& available_formats) noexcept
		{
			for (const auto& format : available_formats)
			{
				if (format.format == vk::Format::eB8G8R8A8Srgb &&
					format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
				{
					return format;
				}
			}

			// is_device_suitable guarantees at least one available format
			return available_formats.front();
		}

		// Resolution of the swapchain images in pixels
		vk::Extent2D choose_swapchain_extent(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D framebuffer_extent) noexcept
		{
			// A width of UINT32_MAX means the surface lets us choose, otherwise the swapchain must use currentExtent
			if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
			{
				return capabilities.currentExtent;
			}

			return {
				.width {
					std::clamp(framebuffer_extent.width,
						capabilities.minImageExtent.width, capabilities.maxImageExtent.width)
				},
				.height {
					std::clamp(framebuffer_extent.height,
						capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
				}
			};
		}

		// Prefers mailbox (no tearing, low latency), otherwise falls back to FIFO (vertical sync)
		vk::PresentModeKHR choose_swapchain_present_mode(const std::vector<vk::PresentModeKHR>& available_present_modes) noexcept
		{
			if (std::ranges::contains(available_present_modes, vk::PresentModeKHR::eMailbox))
			{
				return vk::PresentModeKHR::eMailbox;
			}

			// FIFO is the only mode the Vulkan specification guarantees to be available
			return vk::PresentModeKHR::eFifo;
		}

		// Three images let mailbox keep one on screen, one queued and one being rendered, within the surface limits
		std::uint32_t choose_swapchain_min_image_count(const vk::SurfaceCapabilitiesKHR& capabilities) noexcept
		{
			constexpr std::uint32_t preferred_image_count { 3 };

			const std::uint32_t min_image_count { std::max(preferred_image_count, capabilities.minImageCount) };

			// A maximum of 0 means there's no limit
			if (capabilities.maxImageCount > 0 && capabilities.maxImageCount < min_image_count)
			{
				return capabilities.maxImageCount;
			}

			return min_image_count;
		}
	}

	// ***

	Swapchain::Swapchain(const Device& device, const Window& window)
		: device_ { device }
		, window_ { window }
	{
		create_swapchain();
		create_image_views();
		create_render_finished_semaphores();
	}

	void Swapchain::recreate()
	{
		cleanup_swapchain();

		create_swapchain();
		create_image_views();
		create_render_finished_semaphores();
	}

	const vk::raii::SwapchainKHR& Swapchain::handle() const noexcept
	{
		return swapchain_;
	}

	const vk::SurfaceFormatKHR& Swapchain::surface_format() const noexcept
	{
		return surface_format_;
	}

	vk::Extent2D Swapchain::extent() const noexcept
	{
		return extent_;
	}

	vk::Image Swapchain::image(std::uint32_t index) const noexcept
	{
		return images_[index];
	}

	const vk::raii::ImageView& Swapchain::image_view(std::uint32_t index) const noexcept
	{
		return image_views_[index];
	}

	const vk::raii::Semaphore& Swapchain::render_finished_semaphore(std::uint32_t index) const noexcept
	{
		return render_finished_semaphores_[index];
	}

	void Swapchain::create_swapchain()
	{
		// Retrieve what the surface supports on the selected GPU
		const auto capabilities { device_.physical_device().getSurfaceCapabilitiesKHR(*device_.surface()) };
		const auto available_formats { device_.physical_device().getSurfaceFormatsKHR(*device_.surface()) };
		const auto available_present_modes { device_.physical_device().getSurfacePresentModesKHR(*device_.surface()) };

		// Pick the best settings among the supported ones
		surface_format_ = choose_swapchain_surface_format(available_formats);
		extent_ = choose_swapchain_extent(capabilities, window_.framebuffer_extent());
		const auto present_mode { choose_swapchain_present_mode(available_present_modes) };
		const auto min_image_count { choose_swapchain_min_image_count(capabilities) };

		const vk::SwapchainCreateInfoKHR create_info {
			.surface { *device_.surface() },
			.minImageCount { min_image_count },
			.imageFormat { surface_format_.format },
			.imageColorSpace { surface_format_.colorSpace },
			.imageExtent { extent_ },
			// Always 1 unless developing a stereoscopic 3D application
			.imageArrayLayers { 1 },
			// Images are rendered to directly as color attachments
			.imageUsage { vk::ImageUsageFlagBits::eColorAttachment },
			// A single queue family renders and presents, so no ownership sharing is needed
			.imageSharingMode { vk::SharingMode::eExclusive },
			// Keep the surface's current transform, so no rotation or flip is applied
			.preTransform { capabilities.currentTransform },
			// Ignore the alpha channel when blending with other windows
			.compositeAlpha { vk::CompositeAlphaFlagBitsKHR::eOpaque },
			.presentMode { present_mode },
			// Don't care about the color of pixels hidden behind other windows
			.clipped { vk::True },
			// Not needed: recreate_swapchain waits for the device and destroys the old one before creating this one
			.oldSwapchain { nullptr }
		};

		swapchain_ = device_.logical_device().createSwapchainKHR(create_info);

		// The implementation may create more images than requested
		images_ = swapchain_.getImages();

		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Swapchain:");
			std::println("\t- Format: {}", vk::to_string(surface_format_.format));
			std::println("\t- Color space: {}", vk::to_string(surface_format_.colorSpace));
			std::println("\t- Present mode: {}", vk::to_string(present_mode));
			std::println("\t- Extent: {}x{}", extent_.width, extent_.height);
			std::println("\t- Images: {}", images_.size());
			std::println();
		}
	}

	void Swapchain::create_image_views()
	{
		image_views_.reserve(images_.size());

		for (const auto& image : images_)
		{
			image_views_.push_back(create_image_view(device_, image, surface_format_.format));
		}
	}

	void Swapchain::create_render_finished_semaphores()
	{
		// One per swapchain image, since presentation may still be waiting on it when the next frame is submitted
		// For that same reason existing ones are kept on recreation, and only the missing ones are created
		render_finished_semaphores_.reserve(images_.size());

		while (render_finished_semaphores_.size() < images_.size())
		{
			render_finished_semaphores_.push_back(device_.logical_device().createSemaphore({}));
		}
	}

	void Swapchain::cleanup_swapchain() noexcept
	{
		// Image views must be destroyed before the swapchain that owns their images
		image_views_.clear();
		swapchain_ = nullptr;
	}
}
