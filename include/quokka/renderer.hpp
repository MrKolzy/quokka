#pragma once

#include "quokka/buffer.hpp"
#include "quokka/device.hpp"
#include "quokka/graphics_pipeline.hpp"
#include "quokka/image.hpp"
#include "quokka/mesh.hpp"
#include "quokka/particle_system.hpp"
#include "quokka/swapchain.hpp"
#include "quokka/worker_threads.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <chrono>
#include <cstdint> // std::uint32_t
#include <vector>

namespace quokka
{
	// Uploads the geometry it draws, records the commands of each frame on several threads, submits them and presents the result
	class Renderer
	{
	public:
		Renderer(
			const Device& device,
			const Swapchain& swapchain,
			const GraphicsPipeline& graphics_pipeline,
			const ParticleSystem& particle_system);
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
		void create_secondary_command_buffers();
		void load_model();
		void create_vertex_buffer();
		void create_index_buffer();
		void create_texture_image();
		void create_texture_image_view();
		void create_texture_sampler();
		void create_uniform_buffers();
		void create_descriptor_pool();
		void create_descriptor_sets();
		void create_frame_sync_objects();

		void update_uniform_buffer(void* mapped_memory) const;
		void record_command_buffer(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			std::uint32_t image_index);
		// Begins a secondary command buffer inside the rendering of the primary one, with the viewport and scissor set
		// Called from the recording threads at the same time, so it must only read shared state
		void begin_secondary_command_buffer(const vk::raii::CommandBuffer& command_buffer) const;
		// Called from the recording threads at the same time, so it must only read shared state
		void record_object_draws(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			std::uint32_t first_object,
			std::uint32_t object_count,
			float elapsed_seconds) const;

		const Device& device_;
		const Swapchain& swapchain_;
		const GraphicsPipeline& graphics_pipeline_;
		const ParticleSystem& particle_system_;

		// Declared in creation order: members are destroyed in reverse order,
		// so every object is destroyed before the one it was created from
		// Allocates the memory of the command buffers
		vk::raii::CommandPool command_pool_ { nullptr };
		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> command_buffers_ {};
		// One per recording thread, since a command pool and its command buffers can only be used by one thread at a time
		std::vector<vk::raii::CommandPool> recording_command_pools_ {};
		// Indexed by recording thread and then by frame in flight, executed from the primary command buffer of the frame
		std::vector<std::vector<vk::raii::CommandBuffer>> secondary_command_buffers_ {};
		// One per frame in flight, with the particle draws, executed after the secondary command buffers of the objects
		std::vector<vk::raii::CommandBuffer> particle_command_buffers_ {};

		// Loaded from the model file, uploaded to vertex_buffer_ and index_buffer_
		Mesh mesh_ {};
		// Vertices in device local memory, read by the vertex shader through binding 0
		Buffer vertex_buffer_ {};
		// Indices into vertex_buffer_, so vertices shared by several triangles are stored only once
		Buffer index_buffer_ {};
		// Read from the texture file, which can store its texels in any format
		vk::Format texture_format_ {};
		// Texels of the texture in device local memory, ready to be sampled by the fragment shader
		Image texture_image_ {};
		// How shaders see texture_image_, as a 2D color texture
		vk::raii::ImageView texture_image_view_ { nullptr };
		// How texels are filtered and addressed when sampled, independent of any particular image
		vk::raii::Sampler texture_sampler_ { nullptr };

		// Camera matrices, one per frame in flight so the CPU never overwrites one the GPU may still be reading
		std::vector<Buffer> uniform_buffers_ {};
		// Mapped once and kept mapped, since they're written every frame
		std::vector<void*> mapped_uniform_buffers_ {};

		// Allocates the memory of the descriptor sets
		vk::raii::DescriptorPool descriptor_pool_ { nullptr };
		// One per frame in flight, each pointing to that frame's uniform buffer and to the texture
		std::vector<vk::raii::DescriptorSet> descriptor_sets_ {};

		// Signaled by acquireNextImage, waited on by the submission, one per frame in flight
		std::vector<vk::raii::Semaphore> image_available_semaphores_ {};
		// Signaled by the submission, waited on by the CPU, one per frame in flight
		std::vector<vk::raii::Fence> in_flight_fences_ {};

		// Frame in flight currently being recorded, cycles from 0 to max_frames_in_flight - 1
		std::uint32_t frame_index_ {};

		// The objects rotate according to the time elapsed since then
		std::chrono::steady_clock::time_point start_time_ { std::chrono::steady_clock::now() };
		// When the previous frame was recorded, the particles move according to the time elapsed since then
		std::chrono::steady_clock::time_point previous_frame_time_ { std::chrono::steady_clock::now() };

		// Record the draws of each frame into secondary_command_buffers_
		// Declared last, so its threads are joined before anything they record into is destroyed
		WorkerThreads recording_threads_;
	};
}
