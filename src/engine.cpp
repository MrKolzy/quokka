#include "quokka/engine.hpp"

#include "quokka/constants.hpp"

#include <GLFW/glfw3.h>

#include <algorithm> // std::clamp, std::max, std::ranges
#include <array>
#include <cstddef> // std::size_t
#include <cstdint> // std::uint#_t
#include <cstdio> // stderr
#include <cstring> // std::strcmp
#include <filesystem>
#include <fstream>
#include <limits>
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
		std::vector<const char*> get_required_instance_extensions()
		{
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

		// ***

		// Checked when picking the physical device and enabled when creating the logical device
		constexpr std::array required_device_extensions { vk::KHRSwapchainExtensionName };

		// Index of the first queue family that can execute graphics commands and present to the surface, if any
		std::optional<std::uint32_t> find_queue_family_index(
			const vk::raii::PhysicalDevice& physical_device,
			const vk::raii::SurfaceKHR& surface)
		{
			const auto queue_families { physical_device.getQueueFamilyProperties() };

			for (std::uint32_t index { 0 }; index < queue_families.size(); ++index)
			{
				// A single family for both keeps one queue for everything, which nearly every GPU supports
				if ((queue_families[index].queueFlags & vk::QueueFlagBits::eGraphics) &&
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
					vk::PhysicalDeviceVulkan11Features,
					vk::PhysicalDeviceVulkan13Features>()
			};

			// shaderDrawParameters is needed because Slang translates SV_VertexID using BaseVertex
			const bool has_required_features {
				features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
				features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
				features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering
			};

			return has_suitable_queue_family && has_required_extensions &&
				has_adequate_swapchain_support && has_required_features;
		}

		// ***

		// Prefers 8-bit BGRA sRGB, so the hardware gamma-corrects the shader output on write,
		// otherwise falls back to the first available format
		vk::SurfaceFormatKHR choose_swapchain_surface_format(const std::vector<vk::SurfaceFormatKHR>& available_formats) noexcept
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

		// Resolution of the swapchain images in pixels
		vk::Extent2D choose_swapchain_extent(const vk::SurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) noexcept
		{
			// A width of UINT32_MAX means the surface lets us choose, otherwise the swapchain must use currentExtent
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

		// Prefers mailbox (no tearing, low latency), otherwise falls back to FIFO (vertical sync)
		vk::PresentModeKHR choose_swapchain_present_mode(const std::vector<vk::PresentModeKHR>& available_present_modes) noexcept
		{
			if (std::ranges::contains(available_present_modes, vk::PresentModeKHR::eMailbox))
			{
				return vk::PresentModeKHR::eMailbox;
			}

			// FIFO is the only mode the Vulkan specification guarantees to be available
			return vk::PresentModeKHR::eFifo;
		}

		// Three images let mailbox keep one on screen, one queued and one being rendered, within the surface limits
		std::uint32_t choose_swapchain_min_image_count(const vk::SurfaceCapabilitiesKHR& capabilities) noexcept
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

		// Color, no mipmapping and a single layer, the only subresource swapchain images have
		constexpr vk::ImageSubresourceRange color_subresource_range {
			.aspectMask { vk::ImageAspectFlagBits::eColor },
			.baseMipLevel { 0 },
			.levelCount { 1 },
			.baseArrayLayer { 0 },
			.layerCount { 1 }
		};

		// ***

		// Reads a compiled SPIR-V file as 32-bit words, which also guarantees the alignment Vulkan expects
		std::vector<std::uint32_t> read_shader_file(const std::filesystem::path& path)
		{
			// Start at the end (ate) to get the file size, and read raw bytes without text conversions (binary)
			std::ifstream file { path, std::ios::ate | std::ios::binary };

			if (!file.is_open())
			{
				throw std::runtime_error("Failed to open shader file: " + path.string());
			}

			const auto file_size { static_cast<std::size_t>(file.tellg()) };

			// SPIR-V is a stream of 32-bit words, so an empty file or a partial word means it's invalid
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
		vk::raii::ShaderModule create_shader_module(const vk::raii::Device& device, const std::vector<std::uint32_t>& code)
		{
			const vk::ShaderModuleCreateInfo create_info {
				// Size in bytes, not in words
				.codeSize { code.size() * sizeof(std::uint32_t) },
				.pCode { code.data() }
			};

			return device.createShaderModule(create_info);
		}

		// ***

		// Changes the layout of a color image after the source stages and accesses finish,
		// and before the destination ones start
		void transition_image_layout(
			const vk::raii::CommandBuffer& command_buffer,
			vk::Image image,
			vk::ImageLayout old_layout,
			vk::ImageLayout new_layout,
			vk::PipelineStageFlags2 src_stage_mask,
			vk::AccessFlags2 src_access_mask,
			vk::PipelineStageFlags2 dst_stage_mask,
			vk::AccessFlags2 dst_access_mask) noexcept
		{
			const vk::ImageMemoryBarrier2 barrier {
				.srcStageMask { src_stage_mask },
				.srcAccessMask { src_access_mask },
				.dstStageMask { dst_stage_mask },
				.dstAccessMask { dst_access_mask },
				.oldLayout { old_layout },
				.newLayout { new_layout },
				// The image stays in the same queue family, so there's no ownership transfer
				.srcQueueFamilyIndex { vk::QueueFamilyIgnored },
				.dstQueueFamilyIndex { vk::QueueFamilyIgnored },
				.image { image },
				.subresourceRange { color_subresource_range }
			};

			const vk::DependencyInfo dependency_info {
				.imageMemoryBarrierCount { 1 },
				.pImageMemoryBarriers { &barrier }
			};

			command_buffer.pipelineBarrier2(dependency_info);
		}
	}

	// ***

	void Engine::run()
	{
		initialize_vulkan();
		main_loop();
	}

	void Engine::initialize_vulkan()
	{
		create_instance();
		create_debug_messenger();
		create_surface();
		pick_physical_device();
		create_logical_device();

		create_swapchain();
		create_image_views();

		create_graphics_pipeline();

		create_command_pool();
		create_command_buffers();
		create_frame_sync_objects();
		create_render_finished_semaphores();
	}

	void Engine::create_instance()
	{
		// Application metadata for the driver and the highest Vulkan version to use
		constexpr vk::ApplicationInfo app_info {
			.pApplicationName { constants::app_name },
			.applicationVersion { vk::makeApiVersion(0, 1, 0, 0) },
			.pEngineName { "Quokka Engine" },
			.engineVersion { vk::makeApiVersion(0, 1, 0, 0) },
			.apiVersion { vk::ApiVersion13 }
		};

		const auto required_instance_extensions { get_required_instance_extensions() };
		if constexpr (constants::should_print_debug_output)
		{
			std::println("[Quokka] Required Vulkan instance extensions:");
			for (const auto* extension : required_instance_extensions)
			{
				std::println("\t- {}", extension);
			}
			std::println();
		}

		const auto available_extensions { context_.enumerateInstanceExtensionProperties() };
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

		const auto available_layers { context_.enumerateInstanceLayerProperties() };
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

		instance_ = context_.createInstance(create_info);
	}

	void Engine::create_debug_messenger()
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
		surface_ = vk::raii::SurfaceKHR { instance_, raw_surface };
	}

	void Engine::pick_physical_device()
	{
		const auto physical_devices { instance_.enumeratePhysicalDevices() };
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

		// Prefer a dedicated GPU, otherwise fall back to the first suitable one
		bool has_suitable_device { false };
		for (const auto& physical_device : physical_devices)
		{
			if (!is_device_suitable(physical_device, surface_))
			{
				continue;
			}

			if (!has_suitable_device)
			{
				physical_device_ = physical_device;
				has_suitable_device = true;
			}

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

		// Required by Vulkan even with a single queue, where any value in 0.0 to 1.0 works the same
		constexpr float queue_priority { 0.5f };

		const vk::DeviceQueueCreateInfo queue_create_info {
			.queueFamilyIndex { queue_family_index_ },
			.queueCount { 1 },
			.pQueuePriorities { &queue_priority }
		};

		// Enable the same features checked in is_device_suitable
		const vk::StructureChain<
			vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan11Features,
			vk::PhysicalDeviceVulkan13Features> feature_chain {
			{}, // No Vulkan 1.0 features needed, it only heads the chain
			{ .shaderDrawParameters { vk::True } },
			{ .synchronization2 { vk::True }, .dynamicRendering { vk::True } }
		};

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
		swapchain_surface_format_ = choose_swapchain_surface_format(available_formats);
		swapchain_extent_ = choose_swapchain_extent(capabilities, window_.handle());
		const auto present_mode { choose_swapchain_present_mode(available_present_modes) };
		const auto min_image_count { choose_swapchain_min_image_count(capabilities) };

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
			// Not needed: recreate_swapchain waits for the device and destroys the old one before creating this one
			.oldSwapchain { nullptr }
		};

		swapchain_ = device_.createSwapchainKHR(create_info);

		// The implementation may create more images than requested
		swapchain_images_ = swapchain_.getImages();

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
		swapchain_image_views_.reserve(swapchain_images_.size());

		for (const auto& image : swapchain_images_)
		{
			const vk::ImageViewCreateInfo create_info {
				.image { image },
				// Treat the image as a regular 2D texture
				.viewType { vk::ImageViewType::e2D },
				.format { swapchain_surface_format_.format },
				// Default mapping: each channel is read as itself (identity), no swizzling
				.components {},
				.subresourceRange { color_subresource_range }
			};

			swapchain_image_views_.push_back(device_.createImageView(create_info));
		}
	}

	void Engine::create_graphics_pipeline()
	{
		// Both entry points live in the same compiled module
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

		const std::array shader_stages { vertex_shader_stage_info, fragment_shader_stage_info };

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
			// Mainly used for shadow mapping
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

		// Pipelines are immutable, so changing any state means creating a new one
		// Viewport and scissor are set when recording commands instead, so a resize doesn't require a new pipeline
		constexpr std::array dynamic_states { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

		const vk::PipelineDynamicStateCreateInfo dynamic_state_info {
			.dynamicStateCount { static_cast<std::uint32_t>(dynamic_states.size()) },
			.pDynamicStates { dynamic_states.data() }
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

	void Engine::create_command_pool()
	{
		const vk::CommandPoolCreateInfo create_info {
			// Lets each command buffer be reset individually, since it's re-recorded every frame
			.flags { vk::CommandPoolCreateFlagBits::eResetCommandBuffer },
			// Its command buffers can only be submitted to queues of this family
			.queueFamilyIndex { queue_family_index_ }
		};

		command_pool_ = device_.createCommandPool(create_info);
	}

	void Engine::create_command_buffers()
	{
		const vk::CommandBufferAllocateInfo allocate_info {
			.commandPool { *command_pool_ },
			// Primary buffers are submitted directly to a queue, secondary ones are called from primary ones
			.level { vk::CommandBufferLevel::ePrimary },
			.commandBufferCount { constants::max_frames_in_flight }
		};

		command_buffers_ = device_.allocateCommandBuffers(allocate_info);
	}

	void Engine::create_frame_sync_objects()
	{
		// Created signaled, so the first frames don't wait forever for a submission that never happened
		constexpr vk::FenceCreateInfo fence_info {
			.flags { vk::FenceCreateFlagBits::eSignaled }
		};

		// One per frame in flight, so each frame waits only for its own previous submission
		image_available_semaphores_.reserve(constants::max_frames_in_flight);
		in_flight_fences_.reserve(constants::max_frames_in_flight);

		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			image_available_semaphores_.push_back(device_.createSemaphore({}));
			in_flight_fences_.push_back(device_.createFence(fence_info));
		}
	}

	void Engine::create_render_finished_semaphores()
	{
		// One per swapchain image, since presentation may still be waiting on it when the next frame is submitted
		// For that same reason existing ones are kept on recreation, and only the missing ones are created
		render_finished_semaphores_.reserve(swapchain_images_.size());

		while (render_finished_semaphores_.size() < swapchain_images_.size())
		{
			render_finished_semaphores_.push_back(device_.createSemaphore({}));
		}
	}

	void Engine::recreate_swapchain()
	{
		// A minimized window has an empty framebuffer, so wait until it's restored
		int width {};
		int height {};
		glfwGetFramebufferSize(window_.handle(), &width, &height);

		while (width == 0 || height == 0)
		{
			// The window may be closed while minimized, in which case the main loop exits without recreating anything
			if (glfwWindowShouldClose(window_.handle()))
			{
				return;
			}

			glfwWaitEvents();
			glfwGetFramebufferSize(window_.handle(), &width, &height);
		}

		// The new swapchain queries the current size, so any resize up to this point is already handled
		window_.clear_resized();

		// Nothing may still be using the old swapchain or its image views
		device_.waitIdle();

		cleanup_swapchain();

		create_swapchain();
		create_image_views();
		create_render_finished_semaphores();
	}

	void Engine::cleanup_swapchain() noexcept
	{
		// Image views must be destroyed before the swapchain that owns their images
		swapchain_image_views_.clear();
		swapchain_ = nullptr;
	}

	void Engine::main_loop()
	{
		while (!glfwWindowShouldClose(window_.handle()))
		{
			glfwPollEvents();
			draw_frame();
		}

		// Drawing and presentation are asynchronous, so wait for them before destroying anything
		device_.waitIdle();
	}

	void Engine::draw_frame()
	{
		// Wait indefinitely, the timeout is in nanoseconds
		constexpr auto no_timeout { std::numeric_limits<std::uint64_t>::max() };

		// Objects owned by the current frame in flight
		const auto& in_flight_fence { in_flight_fences_[frame_index_] };
		const auto& image_available_semaphore { image_available_semaphores_[frame_index_] };
		const auto& command_buffer { command_buffers_[frame_index_] };

		// Wait until the GPU finishes this frame's previous submission, so its command buffer can be recorded again
		if (device_.waitForFences(*in_flight_fence, vk::True, no_timeout) != vk::Result::eSuccess)
		{
			throw std::runtime_error("Failed to wait for the in flight fence");
		}

		// The semaphore is signaled once the presentation engine stops reading the image
		// Out of date is returned instead of thrown, here and in presentKHR,
		// because the project defines VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
		const auto [acquire_result, image_index] { swapchain_.acquireNextImage(no_timeout, *image_available_semaphore, nullptr) };

		// The swapchain no longer matches the surface and can't be used, so recreate it and try again next frame
		// Suboptimal is still usable, so the frame is drawn and the swapchain is recreated after presenting
		if (acquire_result == vk::Result::eErrorOutOfDateKHR)
		{
			recreate_swapchain();
			return;
		}

		// Reset only once work is sure to be submitted, otherwise an early return would leave it unsignaled forever
		device_.resetFences(*in_flight_fence);

		// Beginning the command buffer implicitly resets it, since the pool has the reset flag
		record_command_buffer(command_buffer, image_index);

		// Indexed by image, not by frame, since it's tied to the presentation of that image
		const auto& render_finished_semaphore { render_finished_semaphores_[image_index] };

		// Only the color writes wait for the image, the earlier stages can run before it's acquired
		const vk::SemaphoreSubmitInfo wait_semaphore_info {
			.semaphore { *image_available_semaphore },
			.stageMask { vk::PipelineStageFlagBits2::eColorAttachmentOutput }
		};

		// Signaled once every command has finished, including the final transition to the present layout
		const vk::SemaphoreSubmitInfo signal_semaphore_info {
			.semaphore { *render_finished_semaphore },
			.stageMask { vk::PipelineStageFlagBits2::eAllCommands }
		};

		const vk::CommandBufferSubmitInfo command_buffer_info {
			.commandBuffer { *command_buffer }
		};

		const vk::SubmitInfo2 submit_info {
			.waitSemaphoreInfoCount { 1 },
			.pWaitSemaphoreInfos { &wait_semaphore_info },
			.commandBufferInfoCount { 1 },
			.pCommandBufferInfos { &command_buffer_info },
			.signalSemaphoreInfoCount { 1 },
			.pSignalSemaphoreInfos { &signal_semaphore_info }
		};

		// The fence is signaled when the GPU finishes executing the command buffer
		queue_.submit2(submit_info, *in_flight_fence);

		const vk::PresentInfoKHR present_info {
			.waitSemaphoreCount { 1 },
			.pWaitSemaphores { &*render_finished_semaphore },
			.swapchainCount { 1 },
			.pSwapchains { &*swapchain_ },
			.pImageIndices { &image_index }
		};

		const auto present_result { queue_.presentKHR(present_info) };

		// Recreate after presenting, so the semaphores are left in a consistent state
		// The resize flag is also checked, since not every driver reports out of date after a resize
		if (present_result == vk::Result::eErrorOutOfDateKHR ||
			present_result == vk::Result::eSuboptimalKHR ||
			window_.was_resized())
		{
			recreate_swapchain();
		}

		frame_index_ = (frame_index_ + 1) % constants::max_frames_in_flight;
	}

	void Engine::record_command_buffer(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index) const
	{
		// Re-recorded every frame, so each recording is submitted only once
		command_buffer.begin({ .flags { vk::CommandBufferUsageFlagBits::eOneTimeSubmit } });

		// Same stage the acquire semaphore waits on, so the transition happens after the image is acquired
		// The old contents are discarded, so the old layout can be undefined
		transition_image_layout(
			command_buffer,
			swapchain_images_[image_index],
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite
		);

		// Opaque black, used to clear the swapchain image before drawing
		constexpr vk::ClearValue clear_color {
			.color { .float32 { std::array { 0.0f, 0.0f, 0.0f, 1.0f } } }
		};

		// The swapchain image is cleared on load and its result kept on store so it can be presented
		const vk::RenderingAttachmentInfo color_attachment_info {
			.imageView { *swapchain_image_views_[image_index] },
			.imageLayout { vk::ImageLayout::eColorAttachmentOptimal },
			.loadOp { vk::AttachmentLoadOp::eClear },
			.storeOp { vk::AttachmentStoreOp::eStore },
			.clearValue { clear_color }
		};

		// The whole swapchain image, used as both the render area and the scissor
		const vk::Rect2D full_image_area {
			.offset { .x { 0 }, .y { 0 } },
			.extent { swapchain_extent_ }
		};

		// Without framebuffers, the attachments are given when rendering begins
		const vk::RenderingInfo rendering_info {
			.renderArea { full_image_area },
			.layerCount { 1 },
			.colorAttachmentCount { 1 },
			.pColorAttachments { &color_attachment_info }
		};

		command_buffer.beginRendering(rendering_info);

		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphics_pipeline_);

		// Viewport and scissor are dynamic states, so they must be set before drawing
		const vk::Viewport viewport {
			.x { 0.0f },
			.y { 0.0f },
			.width { static_cast<float>(swapchain_extent_.width) },
			.height { static_cast<float>(swapchain_extent_.height) },
			.minDepth { 0.0f },
			.maxDepth { 1.0f }
		};

		command_buffer.setViewport(0, viewport);
		command_buffer.setScissor(0, full_image_area);

		// 3 vertices hardcoded in the shader, 1 instance, starting at the first vertex and instance
		command_buffer.draw(3, 1, 0, 0);

		command_buffer.endRendering();

		// Wait for every color write to finish before the presentation engine reads the image
		transition_image_layout(
			command_buffer,
			swapchain_images_[image_index],
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eBottomOfPipe,
			{}
		);

		command_buffer.end();
	}
}
