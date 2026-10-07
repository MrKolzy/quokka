#include "quokka/swapchain.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/window.hpp"

#include <algorithm> // std::clamp, std::max, std::ranges
#include <array>
#include <cstdint> // std::uint32_t
#include <limits>
#include <print>
#include <stdexcept> // std::runtime_error
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

		// At least one of these is guaranteed to work as a depth attachment, and D32 has the most precision
		// Neither has a stencil component, so the depth aspect alone covers the whole image
		vk::Format choose_depth_format(const vk::raii::PhysicalDevice& physical_device)
		{
			constexpr std::array candidates { vk::Format::eD32Sfloat, vk::Format::eX8D24UnormPack32 };

			for (const auto format : candidates)
			{
				// Optimal tiling, like every image created with create_image
				const auto properties { physical_device.getFormatProperties(format) };
				if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
				{
					return format;
				}
			}

			throw std::runtime_error("Failed to find a supported depth format");
		}

		// The most samples per pixel the GPU supports for both color and depth attachments
		vk::SampleCountFlagBits choose_sample_count(const vk::raii::PhysicalDevice& physical_device) noexcept
		{
			const auto limits { physical_device.getProperties().limits };
			const auto supported_counts { limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts };

			constexpr std::array candidates {
				vk::SampleCountFlagBits::e64,
				vk::SampleCountFlagBits::e32,
				vk::SampleCountFlagBits::e16,
				vk::SampleCountFlagBits::e8
			};

			for (const auto count : candidates)
			{
				if (supported_counts & count)
				{
					return count;
				}
			}

			// The Vulkan specification requires every GPU to support 4 samples for both
			return vk::SampleCountFlagBits::e4;
		}
	}

	// ***

	Swapchain::Swapchain(const Device& device, const Window& window)
		: device_ { device }
		, window_ { window }
		, depth_format_ { choose_depth_format(device.physical_device()) }
		, sample_count_ { choose_sample_count(device.physical_device()) }
	{
		create_swapchain();
		create_image_views();
		create_msaa_color_resources();
		create_depth_resources();
		create_render_finished_semaphores();
	}

	void Swapchain::recreate()
	{
		cleanup_swapchain();

		create_swapchain();
		create_image_views();
		create_msaa_color_resources();
		create_depth_resources();
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

	vk::SampleCountFlagBits Swapchain::sample_count() const noexcept
	{
		return sample_count_;
	}

	vk::Image Swapchain::image(std::uint32_t index) const noexcept
	{
		return images_[index];
	}

	const vk::raii::ImageView& Swapchain::image_view(std::uint32_t index) const noexcept
	{
		return image_views_[index];
	}

	vk::Image Swapchain::msaa_color_image() const noexcept
	{
		return *msaa_color_image_.handle;
	}

	const vk::raii::ImageView& Swapchain::msaa_color_image_view() const noexcept
	{
		return msaa_color_image_view_;
	}

	vk::Format Swapchain::depth_format() const noexcept
	{
		return depth_format_;
	}

	vk::Image Swapchain::depth_image() const noexcept
	{
		return *depth_image_.handle;
	}

	const vk::raii::ImageView& Swapchain::depth_image_view() const noexcept
	{
		return depth_image_view_;
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
			std::println("\t- Samples per pixel: {}", vk::to_string(sample_count_));
			std::println();
		}
	}

	void Swapchain::create_image_views()
	{
		image_views_.reserve(images_.size());

		for (const auto& image : images_)
		{
			image_views_.push_back(create_image_view(device_, image, surface_format_.format, vk::ImageAspectFlagBits::eColor));
		}
	}

	void Swapchain::create_msaa_color_resources()
	{
		// Drawn into instead of the swapchain image, and averaged into it when rendering ends
		// Transient, since only that averaged result is kept and its own contents never need to be stored in memory
		msaa_color_image_ = create_image(
			device_,
			extent_,
			1,
			sample_count_,
			surface_format_.format,
			vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		msaa_color_image_view_ = create_image_view(device_, *msaa_color_image_.handle, surface_format_.format, vk::ImageAspectFlagBits::eColor);
	}

	void Swapchain::create_depth_resources()
	{
		// Only used as an attachment while drawing, never sampled or copied, so a single mip level is enough
		// Every attachment drawn to at once must have the same number of samples as the color image
		depth_image_ = create_image(
			device_,
			extent_,
			1,
			sample_count_,
			depth_format_,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		depth_image_view_ = create_image_view(device_, *depth_image_.handle, depth_format_, vk::ImageAspectFlagBits::eDepth);
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
		// Each view must be destroyed before the image it refers to
		depth_image_view_ = nullptr;
		depth_image_ = {};
		msaa_color_image_view_ = nullptr;
		msaa_color_image_ = {};
		// Image views must be destroyed before the swapchain that owns their images
		image_views_.clear();
		swapchain_ = nullptr;
	}
}
