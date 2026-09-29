#include "quokka/engine.hpp"

// ***

#include "quokka/constants.hpp"

// ***

#include <GLFW/glfw3.h>

// ***

#include <algorithm> // std::clamp, std::max
#include <array>
#include <cstddef> // std::size_t
#include <cstdint> // std::uint32_t
#include <cstdio> // stderr
#include <cstring> // std::strcmp
#include <filesystem>
#include <fstream>
#include <limits> // std::numeric_limits
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

		// Index of the first queue family that can execute graphics commands and present to the surface, if any
		std::optional<std::uint32_t> find_queue_family_index(
			const vk::raii::PhysicalDevice& physical_device,
			const vk::raii::SurfaceKHR& surface)
		{
			const auto queue_families { physical_device.getQueueFamilyProperties() };

			for (std::uint32_t index { 0 }; index < queue_families.size(); ++index)
			{
				// Presentation support is only queried if the family can execute graphics commands
				if ((queue_families[index].queueFlags & vk::QueueFlagBits::eGraphics) &&
					physical_device.getSurfaceSupportKHR(index, *surface))
				{
					return index;
				}
			}

			return std::nullopt;
		}

		// Checks whether a GPU meets every requirement of the engine
		bool is_device_suitable(const vk::raii::PhysicalDevice& physical_device, const vk::raii::SurfaceKHR& surface)
		{
			// Dynamic rendering and the other features used below are core in Vulkan 1.3
			const bool has_vulkan_1_3 { physical_device.getProperties().apiVersion >= vk::ApiVersion13 };

			// At least one queue family must be able to execute graphics commands and present to the surface
			const bool has_suitable_queue_family { find_queue_family_index(physical_device, surface).has_value() };

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

			// The swapchain needs at least one image format and one presentation mode for the surface
			const bool has_adequate_swapchain_support {
				!physical_device.getSurfaceFormatsKHR(*surface).empty() &&
				!physical_device.getSurfacePresentModesKHR(*surface).empty()
			};

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

			return has_vulkan_1_3 && has_suitable_queue_family && has_required_extensions &&
				has_adequate_swapchain_support && has_required_features;
		}

		// ***

		// Prefers 8-bit BGRA with sRGB color space, otherwise falls back to the first available format
		vk::SurfaceFormatKHR choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR>& available_formats) noexcept
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

		// Prefers mailbox (triple buffering, low latency), otherwise falls back to FIFO (vertical sync)
		vk::PresentModeKHR choose_swap_present_mode(const std::vector<vk::PresentModeKHR>& available_present_modes) noexcept
		{
			for (const auto present_mode : available_present_modes)
			{
				if (present_mode == vk::PresentModeKHR::eMailbox)
				{
					return present_mode;
				}
			}

			// FIFO is the only mode the Vulkan specification guarantees to be available
			return vk::PresentModeKHR::eFifo;
		}

		// Requests at least three images so the driver never makes us wait, within the surface limits
		constexpr std::uint32_t choose_swap_min_image_count(const vk::SurfaceCapabilitiesKHR& capabilities) noexcept
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

		// Resolution of the swapchain images in pixels, which may differ from screen coordinates on high DPI displays
		vk::Extent2D choose_swap_extent(const vk::SurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) noexcept
		{
			// The maximum value means the surface lets us choose, otherwise it must match the window
			if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
			{
				return capabilities.currentExtent;
			}

			// Framebuffer size is in pixels, unlike glfwGetWindowSize, which returns screen coordinates
			int width {};
			int height {};
			glfwGetFramebufferSize(window, &width, &height);

			return {
				.width {
					std::clamp(static_cast<std::uint32_t>(width),
						capabilities.minImageExtent.width, capabilities.maxImageExtent.width)
				},
				.height {
					std::clamp(static_cast<std::uint32_t>(height),
						capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
				}
			};
		}

		// ***

		// Reads a compiled SPIR-V file as 32-bit words, which also guarantees the alignment Vulkan expects
		std::vector<std::uint32_t> read_shader_file(const std::filesystem::path& path)
		{
			// Start at the end (ate) to get the file size, and read raw bytes without text conversions (binary)
			std::ifstream file(path, std::ios::ate | std::ios::binary);

			if (!file.is_open())
			{
				throw std::runtime_error("Failed to open shader file: " + path.string());
			}

			const auto file_size { static_cast<std::size_t>(file.tellg()) };

			// SPIR-V is a stream of 32-bit words, so any other size means the file is invalid
			if (file_size == 0 || file_size % sizeof(std::uint32_t) != 0)
			{
				throw std::runtime_error("Invalid SPIR-V file size: " + path.string());
			}

			// Parentheses, not braces: this is the count constructor
			std::vector<std::uint32_t> code(file_size / sizeof(std::uint32_t));

			file.seekg(0, std::ios::beg);
			file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(file_size));

			if (!file)
			{
				throw std::runtime_error("Failed to read shader file: " + path.string());
			}

			return code;
		}

		// Wraps SPIR-V code in a shader module, only needed until the pipeline is created
		[[nodiscard]] vk::raii::ShaderModule create_shader_module(
			const vk::raii::Device& device,
			const std::vector<std::uint32_t>& code)
		{
			// Configuration used to create the shader module
			const vk::ShaderModuleCreateInfo create_info {
				// Size in bytes, not in words
				.codeSize { code.size() * sizeof(std::uint32_t) },
				.pCode { code.data() }
			};

			return device.createShaderModule(create_info);
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
		create_surface();
		pick_physical_device();
		create_logical_device();
		create_swapchain();
		create_image_views();
		create_graphics_pipeline();
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

	void Engine::create_surface()
	{
		// Raw C handle, since GLFW is a C library and doesn't know about vk::raii
		VkSurfaceKHR raw_surface {};

		// GLFW hides the platform-specific surface creation (Win32, X11, Wayland, ...)
		if (glfwCreateWindowSurface(*instance_, window_.handle(), nullptr, &raw_surface) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create window surface");
		}

		// Take ownership of the raw handle so it's destroyed automatically
		surface_ = vk::raii::SurfaceKHR(instance_, raw_surface);
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

		// Prefer a dedicated GPU, otherwise fall back to the first suitable one
		bool has_suitable_device { false };

		for (const auto& physical_device : physical_devices)
		{
			if (!is_device_suitable(physical_device, surface_))
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
		queue_family_index_ = find_queue_family_index(physical_device_, surface_).value();

		// Required by Vulkan even with a single queue, ranges from 0.0 to 1.0
		constexpr float queue_priority { 0.5f };

		// A single queue from the family that executes graphics commands and presents to the surface
		const vk::DeviceQueueCreateInfo queue_create_info {
			.queueFamilyIndex { queue_family_index_ },
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
		queue_ = device_.getQueue(queue_family_index_, 0);
	}

	void Engine::create_swapchain()
	{
		// Retrieve what the surface supports on the selected GPU
		const auto capabilities { physical_device_.getSurfaceCapabilitiesKHR(*surface_) };
		const auto available_formats { physical_device_.getSurfaceFormatsKHR(*surface_) };
		const auto available_present_modes { physical_device_.getSurfacePresentModesKHR(*surface_) };

		// Pick the best settings among the supported ones
		swapchain_surface_format_ = choose_swap_surface_format(available_formats);
		swapchain_extent_ = choose_swap_extent(capabilities, window_.handle());
		const auto present_mode { choose_swap_present_mode(available_present_modes) };
		const auto min_image_count { choose_swap_min_image_count(capabilities) };

		// Configuration used to create the swapchain
		const vk::SwapchainCreateInfoKHR create_info {
			.surface { *surface_ },
			.minImageCount { min_image_count },
			.imageFormat { swapchain_surface_format_.format },
			.imageColorSpace { swapchain_surface_format_.colorSpace },
			.imageExtent { swapchain_extent_ },
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
			// Only needed when recreating the swapchain, for example after a resize
			.oldSwapchain { nullptr }
		};

		swapchain_ = device_.createSwapchainKHR(create_info);

		// The implementation may create more images than requested
		swapchain_images_ = swapchain_.getImages();

		// Print the swapchain configuration for debugging purposes
		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Swapchain:");
			std::println("\t- Format: {}", vk::to_string(swapchain_surface_format_.format));
			std::println("\t- Color space: {}", vk::to_string(swapchain_surface_format_.colorSpace));
			std::println("\t- Present mode: {}", vk::to_string(present_mode));
			std::println("\t- Extent: {}x{}", swapchain_extent_.width, swapchain_extent_.height);
			std::println("\t- Images: {}", swapchain_images_.size());
			std::println();
		}
	}

	void Engine::create_image_views()
	{
		// One view per swapchain image, so reserve the exact amount up front
		swapchain_image_views_.reserve(swapchain_images_.size());

		for (const auto& image : swapchain_images_)
		{
			// Configuration used to create the image view
			const vk::ImageViewCreateInfo create_info {
				.image { image },
				// Treat the image as a regular 2D texture
				.viewType { vk::ImageViewType::e2D },
				.format { swapchain_surface_format_.format },
				// Components are left at their default, which maps each channel to itself (identity)
				// Color target with no mipmapping and a single layer
				.subresourceRange {
					.aspectMask { vk::ImageAspectFlagBits::eColor },
					.baseMipLevel { 0 },
					.levelCount { 1 },
					.baseArrayLayer { 0 },
					.layerCount { 1 }
				}
			};

			swapchain_image_views_.push_back(device_.createImageView(create_info));
		}
	}

	void Engine::create_graphics_pipeline()
	{
		// Vulkan pipelines are almost completely immutable, so any state change requires creating a new one

		// Both entry points live in the same module, which is destroyed at the end of this function
		const auto shader_module { create_shader_module(device_, read_shader_file(constants::shader_path)) };

		// Must match the entry point names in the shader and in the slangc command
		const vk::PipelineShaderStageCreateInfo vertex_shader_stage_info {
			.stage { vk::ShaderStageFlagBits::eVertex },
			.module { *shader_module },
			.pName { "vert_main" }
		};

		const vk::PipelineShaderStageCreateInfo fragment_shader_stage_info {
			.stage { vk::ShaderStageFlagBits::eFragment },
			.module { *shader_module },
			.pName { "frag_main" }
		};

		// Programmable stages of the pipeline, used when creating it
		const std::array shader_stages { vertex_shader_stage_info, fragment_shader_stage_info };

		// Viewport and scissor are set when recording commands, so they don't require recreating the pipeline
		constexpr std::array dynamic_states { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

		const vk::PipelineDynamicStateCreateInfo dynamic_state_info {
			.dynamicStateCount { static_cast<std::uint32_t>(dynamic_states.size()) },
			.pDynamicStates { dynamic_states.data() }
		};

		// No vertex buffers yet, the vertex data is hardcoded in the shader
		constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_info {};

		// Every three vertices form an independent triangle
		constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_info {
			.topology { vk::PrimitiveTopology::eTriangleList }
		};

		// Only the counts, the actual viewport and scissor are dynamic
		constexpr vk::PipelineViewportStateCreateInfo viewport_state_info {
			.viewportCount { 1 },
			.scissorCount { 1 }
		};

		// Turns the triangle into fragments, filling it and discarding the back faces
		constexpr vk::PipelineRasterizationStateCreateInfo rasterization_info {
			// Clamping fragments outside the near and far planes requires a GPU feature
			.depthClampEnable { vk::False },
			// Otherwise geometry would never reach the framebuffer
			.rasterizerDiscardEnable { vk::False },
			// Any mode other than fill requires a GPU feature
			.polygonMode { vk::PolygonMode::eFill },
			.cullMode { vk::CullModeFlagBits::eBack },
			// The shader vertices are listed in clockwise order on screen
			.frontFace { vk::FrontFace::eClockwise },
			// Only useful for shadow mapping
			.depthBiasEnable { vk::False },
			// Any width other than 1.0 requires a GPU feature
			.lineWidth { 1.0f }
		};

		// Multisampling disabled, one sample per pixel
		constexpr vk::PipelineMultisampleStateCreateInfo multisample_info {
			.rasterizationSamples { vk::SampleCountFlagBits::e1 },
			.sampleShadingEnable { vk::False }
		};

		// No blending, the fragment color overwrites the framebuffer, but every channel is still written
		constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment {
			.blendEnable { vk::False },
			.colorWriteMask {
				vk::ColorComponentFlagBits::eR |
				vk::ColorComponentFlagBits::eG |
				vk::ColorComponentFlagBits::eB |
				vk::ColorComponentFlagBits::eA
			}
		};

		// One blend attachment per color attachment, the swapchain image is the only one
		const vk::PipelineColorBlendStateCreateInfo color_blend_info {
			.logicOpEnable { vk::False },
			.attachmentCount { 1 },
			.pAttachments { &color_blend_attachment }
		};

		// Required even when empty, no descriptor set layouts or push constants yet
		constexpr vk::PipelineLayoutCreateInfo pipeline_layout_info {};

		pipeline_layout_ = device_.createPipelineLayout(pipeline_layout_info);

		// Dynamic rendering replaces the render pass and framebuffers, so the pipeline only needs the attachment formats
		// The chain links the rendering info through the pNext of the pipeline create info
		const vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipeline_info_chain {
			{
				.stageCount { static_cast<std::uint32_t>(shader_stages.size()) },
				.pStages { shader_stages.data() },
				.pVertexInputState { &vertex_input_info },
				.pInputAssemblyState { &input_assembly_info },
				.pViewportState { &viewport_state_info },
				.pRasterizationState { &rasterization_info },
				.pMultisampleState { &multisample_info },
				.pColorBlendState { &color_blend_info },
				.pDynamicState { &dynamic_state_info },
				.layout { *pipeline_layout_ },
				// Not used with dynamic rendering
				.renderPass { nullptr }
			},
			{
				// A single color attachment, the swapchain image, with its exact format
				.colorAttachmentCount { 1 },
				.pColorAttachmentFormats { &swapchain_surface_format_.format }
			}
		};

		// No pipeline cache yet, it would let Vulkan reuse data across pipeline creations and program runs
		graphics_pipeline_ = device_.createGraphicsPipeline(nullptr, pipeline_info_chain.get<vk::GraphicsPipelineCreateInfo>());
	}

	void Engine::main_loop()
	{
		while (!glfwWindowShouldClose(window_.handle()))
		{
			glfwPollEvents();
		}
	}
}
