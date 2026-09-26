#include "quokka/engine.hpp"

// ***

#include "quokka/constants.hpp"

// ***

#include <GLFW/glfw3.h>

// ***

#include <cstdint> // std::uint32_t
#include <cstring> // std::strcmp
#include <print>
#include <stdexcept> // std::runtime_error
#include <string>

namespace quokka
{
	Engine::~Engine()
	{
		if (window_)
		{
			glfwDestroyWindow(window_);
		}

		glfwTerminate();
	}

	void Engine::run()
	{
		initialize_window();
		initialize_vulkan();
		main_loop();
	}

	void Engine::initialize_window()
	{
		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}

		// Disable OpenGL context creation, as Vulkan doesn't need one
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// Disable resizing, as it requires recreating the swapchain
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window_ = glfwCreateWindow(constants::width, constants::height, constants::name, nullptr, nullptr);

		if (!window_)
		{
			throw std::runtime_error("Failed to create GLFW window");
		}
	}

	void Engine::initialize_vulkan()
	{
		create_instance();
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

		// Print them for debugging purposes
		std::println("[Quokka] Available Vulkan instance extensions:");
		for (const auto& extension : available_extensions)
		{
			std::println("\t- {}", extension.extensionName.data());
		}
		std::println();

		// Retrieve the Vulkan instance extensions required by GLFW
		std::uint32_t required_extension_count {};
		const auto required_extensions { glfwGetRequiredInstanceExtensions(&required_extension_count) };

		if (!required_extensions)
		{
			throw std::runtime_error("Failed to get the Vulkan instance extensions required by GLFW");
		}

		// Print them for debugging purposes
		std::println("[Quokka] Vulkan instance extensions required by GLFW:");
		for (std::uint32_t i {}; i < required_extension_count; ++i)
		{
			std::println("\t- {}", required_extensions[i]);
		}
		std::println();

		// Verify that every extension required by GLFW is available
		for (std::uint32_t i {}; i < required_extension_count; ++i)
		{
			bool found { false };

			for (const auto& extension : available_extensions)
			{
				if (std::strcmp(extension.extensionName.data(), required_extensions[i]) == 0)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				throw std::runtime_error("Required Vulkan instance extension is not supported: " +
					std::string(required_extensions[i]));
			}
		}

		// Configuration used to create the Vulkan instance
		const vk::InstanceCreateInfo create_info {
			.pApplicationInfo { &app_info },
			.enabledExtensionCount { required_extension_count },
			.ppEnabledExtensionNames { required_extensions }
		};

		instance_ = vk::raii::Instance(context_, create_info);
	}

	void Engine::main_loop()
	{
		while (!glfwWindowShouldClose(window_))
		{
			glfwPollEvents();
		}
	}
}
