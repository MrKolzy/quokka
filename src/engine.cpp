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

		// Disable OpenGL context creation
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// Disable window resizing for now
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window_ = glfwCreateWindow(constants::width, constants::height, "Quokka", nullptr, nullptr);

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
		// Optional information about the application and engine
		constexpr vk::ApplicationInfo app_info {
			.pApplicationName { "Quokka" },
			.applicationVersion { VK_MAKE_VERSION(1, 0, 0) },
			.pEngineName { "Quokka Engine" },
			.engineVersion { VK_MAKE_VERSION(1, 0, 0) },
			.apiVersion { vk::ApiVersion14 }
		};

		// Retrieve and print all available Vulkan instance extensions
		const auto available_extensions { context_.enumerateInstanceExtensionProperties() };

		std::println("[Quokka]: Listing all available Vulkan instance extensions:");
		for (const auto& extension : available_extensions)
		{
			std::println("\t- {}", extension.extensionName.data());
		}
		std::println();

		// Retrieve the Vulkan instance extensions required by GLFW and verify they are supported
		std::uint32_t required_extension_count {};
		const auto required_extensions { glfwGetRequiredInstanceExtensions(&required_extension_count) };

		if (!required_extensions)
		{
			throw std::runtime_error("GLFW could not retrieve the required Vulkan instance extensions");
		}

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
				throw std::runtime_error("Vulkan instance extension required by GLFW is not supported: " +
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
