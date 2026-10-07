#include "quokka/device.hpp"

#include "quokka/constants.hpp"
#include "quokka/window.hpp"

#include <algorithm> // std::ranges
#include <array>
#include <cstdint> // std::uint32_t
#include <cstdio> // stderr
#include <cstring> // std::strcmp
#include <optional>
#include <print>
#include <stdexcept> // std::runtime_error
#include <string>
#include <vector>

namespace quokka
{
	namespace
	{
		// Names are C strings, and == between two char pointers compares addresses instead of the text
		bool is_extension_available(const std::vector<vk::ExtensionProperties>& available_extensions, const char* name) noexcept
		{
			return std::ranges::any_of(available_extensions, [name](const vk::ExtensionProperties& extension)
			{
				return std::strcmp(extension.extensionName.data(), name) == 0;
			});
		}

		bool is_layer_available(const std::vector<vk::LayerProperties>& available_layers, const char* name) noexcept
		{
			return std::ranges::any_of(available_layers, [name](const vk::LayerProperties& layer)
			{
				return std::strcmp(layer.layerName.data(), name) == 0;
			});
		}

		// GLFW's surface extensions for the current platform, plus debug utils when validation is enabled
		std::vector<const char*> get_required_instance_extensions(const Window& window)
		{
			auto extensions { window.required_instance_extensions() };

			// Needed to receive validation layer messages through a callback
			if constexpr (constants::should_enable_validation_layers)
			{
				extensions.push_back(vk::EXTDebugUtilsExtensionName);
			}

			return extensions;
		}

		// Validation layers when they're enabled, none otherwise
		std::vector<const char*> get_required_instance_layers()
		{
			if constexpr (constants::should_enable_validation_layers)
			{
				return std::vector<const char*>(constants::validation_layers.begin(), constants::validation_layers.end());
			}
			else
			{
				return {};
			}
		}

		// Called by the validation layers whenever they have something to report
		VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(
			vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
			vk::DebugUtilsMessageTypeFlagsEXT type,
			const vk::DebugUtilsMessengerCallbackDataEXT* callback_data,
			void* /*user_data*/) noexcept
		{
			std::println(stderr, "[Quokka] Validation layer ({}, {}): {}",
				vk::to_string(severity), vk::to_string(type), callback_data->pMessage);

			// Let the Vulkan call that triggered the message continue normally
			return vk::False;
		}

		// Messenger configuration, shared by instance creation and the debug messenger
		constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info {
			// eVerbose and eInfo are left out, they're mostly noise unless debugging the loader
			.messageSeverity {
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
			},
			.messageType {
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
				vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
				vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
			},
			.pfnUserCallback { &debug_callback }
		};

		vk::raii::Instance create_instance(const vk::raii::Context& context, const Window& window)
		{
			// Application metadata for the driver and the highest Vulkan version to use
			constexpr vk::ApplicationInfo app_info {
				.pApplicationName { constants::app_name },
				.applicationVersion { vk::makeApiVersion(0, 1, 0, 0) },
				.pEngineName { "Quokka Engine" },
				.engineVersion { vk::makeApiVersion(0, 1, 0, 0) },
				.apiVersion { vk::ApiVersion13 }
			};

			const auto required_instance_extensions { get_required_instance_extensions(window) };
			if constexpr (constants::should_print_debug_output)
			{
				std::println("[Quokka] Required Vulkan instance extensions:");
				for (const auto* extension : required_instance_extensions)
				{
					std::println("\t- {}", extension);
				}
				std::println();
			}

			const auto available_extensions { context.enumerateInstanceExtensionProperties() };
			for (const auto* extension : required_instance_extensions)
			{
				if (!is_extension_available(available_extensions, extension))
				{
					throw std::runtime_error("Required Vulkan instance extension is not supported: " + std::string(extension));
				}
			}

			const auto required_instance_layers { get_required_instance_layers() };
			if constexpr (constants::should_print_debug_output)
			{
				std::println("[Quokka] Required Vulkan instance layers:");
				for (const auto* layer : required_instance_layers)
				{
					std::println("\t- {}", layer);
				}

				// Happens if validation layers are disabled while debug output is enabled
				if (required_instance_layers.empty())
				{
					std::println("\t- None");
				}
				std::println();
			}

			const auto available_layers { context.enumerateInstanceLayerProperties() };
			for (const auto* layer : required_instance_layers)
			{
				if (!is_layer_available(available_layers, layer))
				{
					throw std::runtime_error("Required Vulkan instance layer is not supported: " + std::string(layer));
				}
			}

			const vk::InstanceCreateInfo create_info {
				// Also captures messages from instance creation and destruction
				.pNext { constants::should_enable_validation_layers ? &debug_messenger_create_info : nullptr },
				.pApplicationInfo { &app_info },
				.enabledLayerCount { static_cast<std::uint32_t>(required_instance_layers.size()) },
				.ppEnabledLayerNames { required_instance_layers.data() },
				.enabledExtensionCount { static_cast<std::uint32_t>(required_instance_extensions.size()) },
				.ppEnabledExtensionNames { required_instance_extensions.data() }
			};

			return context.createInstance(create_info);
		}

		// Only created in debug builds, otherwise an empty handle
		vk::raii::DebugUtilsMessengerEXT create_debug_messenger(const vk::raii::Instance& instance)
		{
			if constexpr (constants::should_enable_validation_layers)
			{
				return instance.createDebugUtilsMessengerEXT(debug_messenger_create_info);
			}
			else
			{
				return nullptr;
			}
		}

		// ***

		// Checked when picking the physical device and enabled when creating the logical device
		constexpr std::array required_device_extensions { vk::KHRSwapchainExtensionName };

		// Index of the first queue family that can execute graphics and compute commands and present to the surface, if any
		std::optional<std::uint32_t> find_queue_family_index(
			const vk::raii::PhysicalDevice& physical_device,
			const vk::raii::SurfaceKHR& surface)
		{
			const auto queue_families { physical_device.getQueueFamilyProperties() };
			for (std::uint32_t index { 0 }; index < queue_families.size(); ++index)
			{
				// A single family for all of them keeps one queue for everything, which nearly every GPU supports
				if ((queue_families[index].queueFlags & vk::QueueFlagBits::eGraphics) &&
					(queue_families[index].queueFlags & vk::QueueFlagBits::eCompute) &&
					physical_device.getSurfaceSupportKHR(index, *surface))
				{
					return index;
				}
			}

			return std::nullopt;
		}

		bool is_device_suitable(const vk::raii::PhysicalDevice& physical_device, const vk::raii::SurfaceKHR& surface)
		{
			// Dynamic rendering and synchronization2 are core in Vulkan 1.3
			if (physical_device.getProperties().apiVersion < vk::ApiVersion13)
			{
				return false;
			}

			const bool has_suitable_queue_family { find_queue_family_index(physical_device, surface).has_value() };

			const auto available_extensions { physical_device.enumerateDeviceExtensionProperties() };
			const bool has_required_extensions {
				std::ranges::all_of(required_device_extensions, [&available_extensions](const char* extension)
				{
					return is_extension_available(available_extensions, extension);
				})
			};

			// The swapchain needs at least one image format and one presentation mode for the surface
			const bool has_adequate_swapchain_support {
				!physical_device.getSurfaceFormatsKHR(*surface).empty() &&
				!physical_device.getSurfacePresentModesKHR(*surface).empty()
			};

			// The structures are linked through pNext, so a single call fills all of them
			const auto features {
				physical_device.getFeatures2<
					vk::PhysicalDeviceFeatures2,
					vk::PhysicalDeviceVulkan13Features>()
			};

			// Large points let the particles be bigger than one pixel
			const bool has_required_features {
				features.get<vk::PhysicalDeviceFeatures2>().features.largePoints &&
				features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
				features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
				features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering
			};

			return has_suitable_queue_family && has_required_extensions &&
				has_adequate_swapchain_support && has_required_features;
		}

		// Prefers a dedicated GPU, otherwise falls back to the first suitable one
		vk::raii::PhysicalDevice pick_physical_device(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface)
		{
			const auto physical_devices { instance.enumeratePhysicalDevices() };
			if (physical_devices.empty())
			{
				throw std::runtime_error("Failed to find GPUs with Vulkan support");
			}

			if constexpr (constants::should_print_debug_output)
			{
				std::println("[Quokka] Required Vulkan device extensions:");
				for (const auto* extension : required_device_extensions)
				{
					std::println("\t- {}", extension);
				}
				std::println();
			}

			vk::raii::PhysicalDevice selected_physical_device { nullptr };
			bool has_suitable_device { false };
			for (const auto& physical_device : physical_devices)
			{
				if (!is_device_suitable(physical_device, surface))
				{
					continue;
				}

				if (!has_suitable_device)
				{
					selected_physical_device = physical_device;
					has_suitable_device = true;
				}

				if (physical_device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
				{
					selected_physical_device = physical_device;
					break;
				}
			}

			if (!has_suitable_device)
			{
				throw std::runtime_error("Failed to find a suitable GPU");
			}

			if constexpr (constants::should_print_debug_output)
			{
				std::println("[Quokka] Selected physical device:");
				std::println("\t- {}", selected_physical_device.getProperties().deviceName.data());
				std::println();
			}

			return selected_physical_device;
		}

		vk::raii::Device create_logical_device(const vk::raii::PhysicalDevice& physical_device, std::uint32_t queue_family_index)
		{
			// Required by Vulkan even with a single queue, where any value in 0.0 to 1.0 works the same
			constexpr float queue_priority { 0.5f };

			const vk::DeviceQueueCreateInfo queue_create_info {
				.queueFamilyIndex { queue_family_index },
				.queueCount { 1 },
				.pQueuePriorities { &queue_priority }
			};

			// Enable the same features checked in is_device_suitable
			const vk::StructureChain<
				vk::PhysicalDeviceFeatures2,
				vk::PhysicalDeviceVulkan13Features> feature_chain {
				{ .features { .largePoints { vk::True }, .samplerAnisotropy { vk::True } } }, // Vulkan 1.0 features, also the head of the chain
				{ .synchronization2 { vk::True }, .dynamicRendering { vk::True } }
			};

			const vk::DeviceCreateInfo create_info {
				.pNext { &feature_chain.get<vk::PhysicalDeviceFeatures2>() },
				.queueCreateInfoCount { 1 },
				.pQueueCreateInfos { &queue_create_info },
				.enabledExtensionCount { static_cast<std::uint32_t>(required_device_extensions.size()) },
				.ppEnabledExtensionNames { required_device_extensions.data() }
			};

			return physical_device.createDevice(create_info);
		}
	}

	// ***

	Device::Device(const Window& window)
		: instance_ { create_instance(context_, window) }
		, debug_messenger_ { create_debug_messenger(instance_) }
		, surface_ { window.create_surface(instance_) }
		, physical_device_ { pick_physical_device(instance_, surface_) }
		// The selected GPU already passed is_device_suitable, so this always has a value
		, queue_family_index_ { find_queue_family_index(physical_device_, surface_).value() }
		, logical_device_ { create_logical_device(physical_device_, queue_family_index_) }
		// Only one queue was requested from the family, so its index is 0
		, queue_ { logical_device_.getQueue(queue_family_index_, 0) }
	{
	}

	const vk::raii::SurfaceKHR& Device::surface() const noexcept
	{
		return surface_;
	}

	const vk::raii::PhysicalDevice& Device::physical_device() const noexcept
	{
		return physical_device_;
	}

	std::uint32_t Device::queue_family_index() const noexcept
	{
		return queue_family_index_;
	}

	const vk::raii::Device& Device::logical_device() const noexcept
	{
		return logical_device_;
	}

	const vk::raii::Queue& Device::queue() const noexcept
	{
		return queue_;
	}

	std::uint32_t Device::find_memory_type(std::uint32_t type_filter, vk::MemoryPropertyFlags properties) const
	{
		const auto memory_properties { physical_device_.getMemoryProperties() };

		for (std::uint32_t index { 0 }; index < memory_properties.memoryTypeCount; ++index)
		{
			// Bit i of the filter is set when memory type i can back the resource
			const bool is_type_allowed { (type_filter & (1u << index)) != 0 };
			const bool has_properties { (memory_properties.memoryTypes[index].propertyFlags & properties) == properties };
			if (is_type_allowed && has_properties)
			{
				return index;
			}
		}

		throw std::runtime_error("Failed to find a suitable memory type");
	}
}
