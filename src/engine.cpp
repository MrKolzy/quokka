#include "quokka/engine.hpp"

// ***

#include "quokka/constants.hpp"

// ***

#include <GLFW/glfw3.h>

// ***

#include <cstdint> // std::uint32_t
#include <cstdio> // stderr
#include <cstring> // std::strcmp
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

	void Engine::main_loop()
	{
		while (!glfwWindowShouldClose(window_.handle()))
		{
			glfwPollEvents();
		}
	}
}
