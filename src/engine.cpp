#include "quokka/engine.hpp"

// ***

#include "quokka/constants.hpp"

// ***

#include <GLFW/glfw3.h>

// ***

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
	// Helpers only visible inside this file
	namespace
	{
		// Vulkan instance extensions required by GLFW, plus debug utils when validation is enabled
		std::vector<const char*> get_required_extensions()
		{
			// Retrieve the Vulkan instance extensions required by GLFW
			std::uint32_t glfw_extension_count {};
			const auto glfw_extensions { glfwGetRequiredInstanceExtensions(&glfw_extension_count) };

			if (!glfw_extensions)
			{
				throw std::runtime_error("Failed to get the Vulkan instance extensions required by GLFW");
			}

			// Parentheses, not braces: this is the iterator-range constructor
			std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

			// Needed to receive validation layer messages through a callback
			if constexpr (constants::should_enable_validation_layers)
			{
				extensions.push_back(vk::EXTDebugUtilsExtensionName);
			}

			return extensions;
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
			// Add eVerbose for detailed loader diagnostics (layer and driver loading), otherwise it's just noise
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

		// ***

		// Device extensions every candidate GPU must support
		constexpr std::array required_device_extensions { vk::KHRSwapchainExtensionName };

		// Index of the first queue family that can execute graphics commands, if any
		std::optional<std::uint32_t> find_graphics_queue_family_index(const vk::raii::PhysicalDevice& physical_device)
		{
			const auto queue_families { physical_device.getQueueFamilyProperties() };

			for (std::uint32_t index { 0 }; index < queue_families.size(); ++index)
			{
				if (queue_families[index].queueFlags & vk::QueueFlagBits::eGraphics)
				{
					return index;
				}
			}

			return std::nullopt;
		}

		// Checks whether a GPU meets every requirement of the engine
		bool is_device_suitable(const vk::raii::PhysicalDevice& physical_device)
		{
			// Dynamic rendering and the other features used below are core in Vulkan 1.3
			const bool has_vulkan_1_3 { physical_device.getProperties().apiVersion >= vk::ApiVersion13 };

			// At least one queue family must be able to execute graphics commands
			const bool has_graphics_queue { find_graphics_queue_family_index(physical_device).has_value() };

			// Every required device extension must be available
			const auto available_extensions { physical_device.enumerateDeviceExtensionProperties() };

			bool has_required_extensions { true };

			for (const auto* required_extension : required_device_extensions)
			{
				bool is_available { false };

				for (const auto& extension : available_extensions)
				{
					if (std::strcmp(extension.extensionName.data(), required_extension) == 0)
					{
						is_available = true;
						break;
					}
				}

				if (!is_available)
				{
					has_required_extensions = false;
					break;
				}
			}

			// Query all the feature structures we need in a single call
			const auto features { physical_device.getFeatures2<
				vk::PhysicalDeviceFeatures2,
				vk::PhysicalDeviceVulkan11Features,
				vk::PhysicalDeviceVulkan13Features,
				vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
			};

			const bool has_required_features {
				features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
				features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
				features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState
			};

			return has_vulkan_1_3 && has_graphics_queue && has_required_extensions && has_required_features;
		}
	}
}

namespace quokka
{
	void Engine::run()
	{
		initialize_vulkan();
		main_loop();
	}

	void Engine::initialize_vulkan()
	{
		create_instance();
		setup_debug_messenger();
		pick_physical_device();
		create_logical_device();
	}

	void Engine::create_instance()
	{
		// Application metadata for the driver and the highest Vulkan version to use
		constexpr vk::ApplicationInfo app_info {
			.pApplicationName { constants::name },
			.applicationVersion { vk::makeApiVersion(0, 1, 0, 0) },
			.pEngineName { "Quokka Engine" },
			.engineVersion { vk::makeApiVersion(0, 1, 0, 0) },
			.apiVersion { vk::ApiVersion14 }
		};

		// Retrieve all available Vulkan instance extensions
		const auto available_extensions { context_.enumerateInstanceExtensionProperties() };

		// Extensions required by GLFW, plus debug utils when validation is enabled
		const auto required_extensions { get_required_extensions() };

		// Print them for debugging purposes
		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Required Vulkan instance extensions:");
			for (const auto* extension : required_extensions)
			{
				std::println("\t- {}", extension);
			}
			std::println();
		}

		// Verify that every required extension is available
		for (const auto* required_extension : required_extensions)
		{
			bool is_available { false };

			for (const auto& extension : available_extensions)
			{
				if (std::strcmp(extension.extensionName.data(), required_extension) == 0)
				{
					is_available = true;
					break;
				}
			}

			if (!is_available)
			{
				throw std::runtime_error("Required Vulkan instance extension is not supported: " +
					std::string(required_extension));
			}
		}

		// Retrieve all available Vulkan instance layers
		const auto available_layers { context_.enumerateInstanceLayerProperties() };

		// Validation layers to enable (empty in release builds)
		std::vector<const char*> required_layers {};
		if constexpr (constants::should_enable_validation_layers)
		{
			required_layers.assign(constants::validation_layers.begin(), constants::validation_layers.end());
		}

		// Print them for debugging purposes
		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Required Vulkan instance layers:");
			for (const auto* layer : required_layers)
			{
				std::println("\t- {}", layer);
			}

			// Happens if validation layers are disabled while debug output is enabled
			if (required_layers.empty())
			{
				std::println("\t- None");
			}
			std::println();
		}

		// Verify that every required layer is available
		for (const auto* required_layer : required_layers)
		{
			bool is_available { false };

			for (const auto& layer : available_layers)
			{
				if (std::strcmp(layer.layerName.data(), required_layer) == 0)
				{
					is_available = true;
					break;
				}
			}

			if (!is_available)
			{
				throw std::runtime_error("Required Vulkan layer is not supported: " + std::string(required_layer));
			}
		}

		// Configuration used to create the Vulkan instance
		const vk::InstanceCreateInfo create_info {
			// Also captures messages from instance creation and destruction
			.pNext { constants::should_enable_validation_layers ? &debug_messenger_create_info : nullptr },
			.pApplicationInfo { &app_info },
			.enabledLayerCount { static_cast<std::uint32_t>(required_layers.size()) },
			.ppEnabledLayerNames { required_layers.data() },
			.enabledExtensionCount { static_cast<std::uint32_t>(required_extensions.size()) },
			.ppEnabledExtensionNames { required_extensions.data() }
		};

		instance_ = context_.createInstance(create_info);
	}

	void Engine::setup_debug_messenger()
	{
		if constexpr (constants::should_enable_validation_layers)
		{
			debug_messenger_ = instance_.createDebugUtilsMessengerEXT(debug_messenger_create_info);
		}
	}

	void Engine::pick_physical_device()
	{
		// Retrieve every GPU that supports Vulkan
		const auto physical_devices { instance_.enumeratePhysicalDevices() };

		if (physical_devices.empty())
		{
			throw std::runtime_error("Failed to find GPUs with Vulkan support");
		}

		// Print the device extensions every candidate GPU must support for debugging purposes
		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Required Vulkan device extensions:");
			for (const auto* extension : required_device_extensions)
			{
				std::println("\t- {}", extension);
			}
			std::println();
		}

		// Print every GPU found for debugging purposes
		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Available physical devices:");
			for (const auto& physical_device : physical_devices)
			{
				std::println("\t- {}", physical_device.getProperties().deviceName.data());
			}
			std::println();
		}

		// Prefer a dedicated GPU, otherwise fall back to the first suitable one
		bool has_suitable_device { false };

		for (const auto& physical_device : physical_devices)
		{
			if (!is_device_suitable(physical_device))
			{
				continue;
			}

			// Remember the first suitable GPU in case there's no dedicated one
			if (!has_suitable_device)
			{
				physical_device_ = physical_device;
				has_suitable_device = true;
			}

			// A dedicated GPU is the best choice, so stop searching
			if (physical_device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
			{
				physical_device_ = physical_device;
				break;
			}
		}

		if (!has_suitable_device)
		{
			throw std::runtime_error("Failed to find a suitable GPU");
		}

		// Print the selected GPU for debugging purposes
		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Selected physical device:");
			std::println("\t- {}", physical_device_.getProperties().deviceName.data());
			std::println();
		}
	}

	void Engine::create_logical_device()
	{
		// The selected GPU already passed is_device_suitable, so this always has a value
		graphics_queue_family_index_ = find_graphics_queue_family_index(physical_device_).value();

		// Required by Vulkan even with a single queue, ranges from 0.0 to 1.0
		constexpr float queue_priority { 0.5f };

		// A single queue from the graphics queue family
		const vk::DeviceQueueCreateInfo queue_create_info {
			.queueFamilyIndex { graphics_queue_family_index_ },
			.queueCount { 1 },
			.pQueuePriorities { &queue_priority }
		};

		// Enable the same features checked in is_device_suitable
		const vk::StructureChain<
			vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan11Features,
			vk::PhysicalDeviceVulkan13Features,
			vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> feature_chain {
			{},
			{ .shaderDrawParameters { vk::True } },
			{ .dynamicRendering { vk::True } },
			{ .extendedDynamicState { vk::True } }
		};

		// Configuration used to create the logical device
		const vk::DeviceCreateInfo create_info {
			.pNext { &feature_chain.get<vk::PhysicalDeviceFeatures2>() },
			.queueCreateInfoCount { 1 },
			.pQueueCreateInfos { &queue_create_info },
			.enabledExtensionCount { static_cast<std::uint32_t>(required_device_extensions.size()) },
			.ppEnabledExtensionNames { required_device_extensions.data() }
		};

		device_ = physical_device_.createDevice(create_info);

		// Only one queue was requested from the family, so its index is 0
		graphics_queue_ = device_.getQueue(graphics_queue_family_index_, 0);
	}

	void Engine::main_loop()
	{
		while (!glfwWindowShouldClose(window_.handle()))
		{
			glfwPollEvents();
		}
	}
}
