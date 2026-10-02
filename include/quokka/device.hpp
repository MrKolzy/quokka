#pragma once

#include "quokka/window.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t

namespace quokka
{
	// Owns the connection between the application and the GPU, from the instance to the queue
	class Device
	{
	public:
		explicit Device(const Window& window);
		~Device() = default;

		// Neither copyable nor movable: the other modules keep references to it
		Device(const Device&) = delete;
		Device& operator=(const Device&) = delete;
		Device(Device&&) = delete;
		Device& operator=(Device&&) = delete;

		[[nodiscard]] const vk::raii::SurfaceKHR& surface() const noexcept;
		[[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const noexcept;
		[[nodiscard]] std::uint32_t queue_family_index() const noexcept;
		[[nodiscard]] const vk::raii::Device& logical_device() const noexcept;
		[[nodiscard]] const vk::raii::Queue& queue() const noexcept;

	private:
		// Declared in creation order: members are initialized in this order and destroyed in reverse
		// Loads the Vulkan library and exposes functions that don't require an instance
		vk::raii::Context context_ {};
		vk::raii::Instance instance_;

		// Routes validation layer messages to our callback, only created in debug builds
		vk::raii::DebugUtilsMessengerEXT debug_messenger_;

		// Connection between Vulkan and the window
		vk::raii::SurfaceKHR surface_;

		// GPU selected to run the engine, implicitly destroyed with the instance
		vk::raii::PhysicalDevice physical_device_;

		// Family of queue_, also needed to create the command pool
		std::uint32_t queue_family_index_;
		// Used to create most other Vulkan objects
		vk::raii::Device logical_device_;
		// Submits graphics commands and presents images, implicitly destroyed with the device
		vk::raii::Queue queue_;
	};
}
