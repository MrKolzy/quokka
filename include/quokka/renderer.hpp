#pragma once

#include "quokka/buffer.hpp"
#include "quokka/device.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/swapchain.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <chrono>
#include <cstdint> // std::uint32_t
#include <vector>

namespace quokka
{
	// Uploads the geometry it draws, records the commands of each frame, submits them and presents the result
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
		void create_index_buffer();
		void create_uniform_buffers();
		void create_descriptor_pool();
		void create_descriptor_sets();
		void create_frame_sync_objects();

		void update_uniform_buffer(void* mapped_memory) const;
		void record_command_buffer(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			std::uint32_t image_index) const;

		const Device& device_;
		const Swapchain& swapchain_;
		const GraphicsPipeline& graphics_pipeline_;

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from
		// Allocates the memory of the command buffers
		vk::raii::CommandPool command_pool_ { nullptr };
		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> command_buffers_ {};

		// Vertices in device local memory, read by the vertex shader through binding 0
		Buffer vertex_buffer_ {};
		// Indices into vertex_buffer_, so vertices shared by several triangles are stored only once
		Buffer index_buffer_ {};

		// Transformation matrices, one per frame in flight so the CPU never overwrites one the GPU may still be reading
		std::vector<Buffer> uniform_buffers_ {};
		// Mapped once and kept mapped, since they're written every frame
		std::vector<void*> mapped_uniform_buffers_ {};

		// Allocates the memory of the descriptor sets
		vk::raii::DescriptorPool descriptor_pool_ { nullptr };
		// One per frame in flight, each pointing to that frame's uniform buffer
		std::vector<vk::raii::DescriptorSet> descriptor_sets_ {};

		// Signaled by acquireNextImage, waited on by the submission, one per frame in flight
		std::vector<vk::raii::Semaphore> image_available_semaphores_ {};
		// Signaled by the submission, waited on by the CPU, one per frame in flight
		std::vector<vk::raii::Fence> in_flight_fences_ {};

		// Frame in flight currently being recorded, cycles from 0 to max_frames_in_flight - 1
		std::uint32_t frame_index_ {};

		// The rectangle rotates according to the time elapsed since then
		std::chrono::steady_clock::time_point start_time_ { std::chrono::steady_clock::now() };
	};
}
