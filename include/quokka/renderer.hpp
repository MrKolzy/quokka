#pragma once

#include "quokka/device.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/swapchain.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector>

namespace quokka
{
	// Records the commands of each frame, submits them and presents the result
	class Renderer
	{
	public:
		Renderer(const Device& device, const Swapchain& swapchain, const GraphicsPipeline& graphics_pipeline);
		~Renderer();

		// Neither copyable nor movable: holds references to the modules it draws with
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
		Renderer& operator=(Renderer&&) = delete;

		// Returns true when the swapchain must be recreated before the next frame
		[[nodiscard]] bool draw_frame();

	private:
		void create_command_pool();
		void create_command_buffers();
		void create_vertex_buffer();
		void create_frame_sync_objects();

		void record_command_buffer(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index) const;

		const Device& device_;
		const Swapchain& swapchain_;
		const GraphicsPipeline& graphics_pipeline_;

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from
		// Allocates the memory of the command buffers
		vk::raii::CommandPool command_pool_ { nullptr };
		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> command_buffers_ {};

		// Vertices of the triangle, read by the vertex shader through binding 0
		vk::raii::Buffer vertex_buffer_ { nullptr };
		// Backs vertex_buffer_, freed first since it's allocated after it
		vk::raii::DeviceMemory vertex_buffer_memory_ { nullptr };

		// Signaled by acquireNextImage, waited on by the submission, one per frame in flight
		std::vector<vk::raii::Semaphore> image_available_semaphores_ {};
		// Signaled by the submission, waited on by the CPU, one per frame in flight
		std::vector<vk::raii::Fence> in_flight_fences_ {};

		// Frame in flight currently being recorded, cycles from 0 to max_frames_in_flight - 1
		std::uint32_t frame_index_ {};
	};
}
