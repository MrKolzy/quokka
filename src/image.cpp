#include "quokka/image.hpp"

namespace quokka
{
	Image create_image(
		const Device& device,
		vk::Extent2D extent,
		std::uint32_t mip_levels,
		vk::Format format,
		vk::ImageUsageFlags usage,
		vk::MemoryPropertyFlags properties)
	{
		const vk::ImageCreateInfo image_info {
			.imageType { vk::ImageType::e2D },
			.format { format },
			.extent { .width { extent.width }, .height { extent.height }, .depth { 1 } },
			.mipLevels { mip_levels },
			// No array layers yet
			.arrayLayers { 1 },
			.samples { vk::SampleCountFlagBits::e1 },
			// Texels in an implementation defined order that's faster for the GPU, but the CPU can't access them directly
			.tiling { vk::ImageTiling::eOptimal },
			.usage { usage },
			// The engine uses a single queue, so the image is never shared between queue families
			.sharingMode { vk::SharingMode::eExclusive },
			// The first transition discards the contents anyway
			.initialLayout { vk::ImageLayout::eUndefined }
		};

		Image image {};
		image.handle = device.logical_device().createImage(image_info);

		// The size may be larger than requested, and memoryTypeBits limits which memory types can back the image
		const auto memory_requirements { image.handle.getMemoryRequirements() };

		const vk::MemoryAllocateInfo allocate_info {
			.allocationSize { memory_requirements.size },
			.memoryTypeIndex { device.find_memory_type(memory_requirements.memoryTypeBits, properties) }
		};

		image.memory = device.logical_device().allocateMemory(allocate_info);

		// Offset 0, since the whole allocation belongs to this image
		image.handle.bindMemory(*image.memory, 0);

		return image;
	}

	vk::raii::ImageView create_image_view(
		const Device& device,
		vk::Image image,
		vk::Format format,
		vk::ImageAspectFlags aspect)
	{
		const vk::ImageViewCreateInfo create_info {
			.image { image },
			// Treat the image as a regular 2D texture
			.viewType { vk::ImageViewType::e2D },
			.format { format },
			// Default mapping: each channel is read as itself (identity), no swizzling
			.components {},
			.subresourceRange { full_subresource_range(aspect) }
		};

		return device.logical_device().createImageView(create_info);
	}
}
