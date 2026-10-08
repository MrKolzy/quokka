#include "quokka/swapchain.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/window.hpp"

#include <algorithm> // std::clamp, std::max, std::ranges::contains
#include <array> // std::array
#include <cstdint> // std::uint32_t
#include <limits> // std::numeric_limits
#include <print> // std::println
#include <stdexcept> // std::runtime_error
#include <vector> // std::vector

namespace quokka
{
	namespace
	{
		// Prefers sRGB, so the hardware gamma-corrects the shader output on write
		vk::SurfaceFormatKHR choose_swapchain_surface_format(
			const std::vector<vk::SurfaceFormatKHR>& available_formats) noexcept
		{
			for (const auto& format : available_formats)
			{
				if (format.format == vk::Format::eB8G8R8A8Srgb &&
					format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
				{
					return format;
				}
			}

			// is_device_suitable guarantees it's not empty
			return available_formats.front();
		}

		vk::Extent2D choose_swapchain_extent(
			const vk::SurfaceCapabilitiesKHR& capabilities,
			vk::Extent2D framebuffer_extent) noexcept
		{
			// UINT32_MAX means the surface lets us choose, otherwise currentExtent must be used
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

		vk::PresentModeKHR choose_swapchain_present_mode(
			const std::vector<vk::PresentModeKHR>& available_present_modes) noexcept
		{
			if (std::ranges::contains(available_present_modes, constants::preferred_present_mode))
			{
				return constants::preferred_present_mode;
			}

			// Vertical sync, the only mode the specification guarantees
			return vk::PresentModeKHR::eFifo;
		}

		// Three images let mailbox keep one on screen, one queued and one being rendered
		std::uint32_t choose_swapchain_min_image_count(
			const vk::SurfaceCapabilitiesKHR& capabilities) noexcept
		{
			constexpr std::uint32_t preferred_image_count { 3 };

			const std::uint32_t min_image_count {
				std::max(preferred_image_count, capabilities.minImageCount)
			};

			// A maximum of 0 means there's no limit
			if (capabilities.maxImageCount > 0 && capabilities.maxImageCount < min_image_count)
			{
				return capabilities.maxImageCount;
			}

			return min_image_count;
		}

		// One of them is always supported as a depth attachment, and D32 is the most precise
		// No stencil, so the depth aspect alone covers the whole image
		vk::Format choose_depth_format(const vk::raii::PhysicalDevice& physical_device)
		{
			constexpr std::array candidates {
				vk::Format::eD32Sfloat,
				vk::Format::eX8D24UnormPack32
			};

			for (const auto format : candidates)
			{
				// Optimal tiling, like every image created with create_image
				const auto properties { physical_device.getFormatProperties(format) };
				if (properties.optimalTilingFeatures &
					vk::FormatFeatureFlagBits::eDepthStencilAttachment)
				{
					return format;
				}
			}

			throw std::runtime_error("Failed to find a supported depth format");
		}

		// Highest count supported by both color and depth attachments
		vk::SampleCountFlagBits choose_sample_count(
			const vk::raii::PhysicalDevice& physical_device) noexcept
		{
			const auto limits { physical_device.getProperties().limits };
			const auto supported_counts {
				limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts
			};

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

			// Every GPU must support 4 samples for color and depth
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

	const vk::raii::Semaphore& Swapchain::render_finished_semaphore(
		std::uint32_t index) const noexcept
	{
		return render_finished_semaphores_[index];
	}

	void Swapchain::create_swapchain()
	{
		const auto& physical_device { device_.physical_device() };
		const auto surface { *device_.surface() };

		const auto capabilities { physical_device.getSurfaceCapabilitiesKHR(surface) };
		const auto available_formats { physical_device.getSurfaceFormatsKHR(surface) };
		const auto available_present_modes { physical_device.getSurfacePresentModesKHR(surface) };

		surface_format_ = choose_swapchain_surface_format(available_formats);
		extent_ = choose_swapchain_extent(capabilities, window_.framebuffer_extent());
		const auto present_mode { choose_swapchain_present_mode(available_present_modes) };
		const auto min_image_count { choose_swapchain_min_image_count(capabilities) };

		const vk::SwapchainCreateInfoKHR create_info {
			.surface { surface },
			.minImageCount { min_image_count },
			.imageFormat { surface_format_.format },
			.imageColorSpace { surface_format_.colorSpace },
			.imageExtent { extent_ },
			// More than 1 only for stereoscopic 3D
			.imageArrayLayers { 1 },
			// Written by the MSAA resolve, which counts as a color attachment write
			.imageUsage { vk::ImageUsageFlagBits::eColorAttachment },
			// One queue family renders and presents, so no sharing is needed
			.imageSharingMode { vk::SharingMode::eExclusive },
			.preTransform { capabilities.currentTransform },
			.compositeAlpha { vk::CompositeAlphaFlagBitsKHR::eOpaque },
			.presentMode { present_mode },
			// Pixels hidden by other windows may be skipped
			.clipped { vk::True },
			// Not needed, because recreate destroys the old swapchain before creating this one
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
			image_views_.push_back(create_image_view(
				device_, image, surface_format_.format, vk::ImageAspectFlagBits::eColor));
		}
	}

	void Swapchain::create_msaa_color_resources()
	{
		// Resolved into the swapchain image when rendering ends
		// Transient, because its samples are discarded once they're averaged
		msaa_color_image_ = create_image(
			device_,
			extent_,
			1,
			sample_count_,
			surface_format_.format,
			vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		msaa_color_image_view_ = create_image_view(
			device_,
			*msaa_color_image_.handle,
			surface_format_.format,
			vk::ImageAspectFlagBits::eColor
		);
	}

	void Swapchain::create_depth_resources()
	{
		// Must have the same sample count as the color attachment it's drawn with
		depth_image_ = create_image(
			device_,
			extent_,
			1,
			sample_count_,
			depth_format_,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		depth_image_view_ = create_image_view(
			device_,
			*depth_image_.handle,
			depth_format_,
			vk::ImageAspectFlagBits::eDepth
		);
	}

	void Swapchain::create_render_finished_semaphores()
	{
		// One per image, since only reacquiring it proves its presentation is done
		// Kept on recreation, because waiting for the device doesn't cover presentation
		render_finished_semaphores_.reserve(images_.size());

		while (render_finished_semaphores_.size() < images_.size())
		{
			render_finished_semaphores_.push_back(device_.logical_device().createSemaphore({}));
		}
	}

	void Swapchain::cleanup_swapchain() noexcept
	{
		// Reverse creation order, like the destructor
		depth_image_view_ = nullptr;
		depth_image_ = {};
		msaa_color_image_view_ = nullptr;
		msaa_color_image_ = {};
		image_views_.clear();
		swapchain_ = nullptr;
	}
}
