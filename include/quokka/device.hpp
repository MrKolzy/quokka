#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t

namespace quokka
{
	class Window;

	class Device
	{
	public:
		explicit Device(const Window& window);
		~Device() = default;

		// Other modules keep references to it, so it can't be copied or moved
		Device(const Device&) = delete;
		Device& operator=(const Device&) = delete;
		Device(Device&&) = delete;
		Device& operator=(Device&&) = delete;

		[[nodiscard]] const vk::raii::SurfaceKHR& surface() const noexcept;
		[[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const noexcept;
		[[nodiscard]] std::uint32_t queue_family_index() const noexcept;
		[[nodiscard]] const vk::raii::Device& logical_device() const noexcept;
		[[nodiscard]] const vk::raii::Queue& queue() const noexcept;

		// Index of a memory type allowed by type_filter that has all the required_properties
		[[nodiscard]] std::uint32_t find_memory_type(
			std::uint32_t type_filter, vk::MemoryPropertyFlags required_properties) const;

	private:
		// Each member uses the ones above it and is destroyed before them, so don't reorder
		vk::raii::Context context_ {};
		vk::raii::Instance instance_;

		// Null in release builds because validation layers are only enabled in debug
		vk::raii::DebugUtilsMessengerEXT debug_messenger_;

		vk::raii::SurfaceKHR surface_;

		// Implicitly destroyed with the instance
		vk::raii::PhysicalDevice physical_device_;

		// Also needed to create command pools
		std::uint32_t queue_family_index_;

		vk::raii::Device logical_device_;

		// Handles graphics, compute and present, and is implicitly destroyed with the device
		vk::raii::Queue queue_;
	};
}
