#pragma once

#include "quokka/buffer.hpp"
#include "quokka/image.hpp"
#include "quokka/mesh.hpp"
#include "quokka/worker_threads.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <chrono> // std::chrono::steady_clock
#include <cstdint> // std::uint32_t
#include <vector> // std::vector

namespace quokka
{
	class Device;
	class GraphicsPipeline;
	class ParticleSystem;
	class Swapchain;

	class Renderer
	{
	public:
		Renderer(
			const Device& device,
			const Swapchain& swapchain,
			const GraphicsPipeline& graphics_pipeline,
			const ParticleSystem& particle_system);
		~Renderer();

		// recording_threads_ can't be copied or moved, so neither can it
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

		// Called from several recording threads at once, so they must only read shared state
		void begin_secondary_command_buffer(const vk::raii::CommandBuffer& command_buffer) const;
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

		// Each one is destroyed before the object it came from, so don't reorder
		vk::raii::CommandPool command_pool_ { nullptr };

		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> primary_command_buffers_ {};

		// One per recording thread, because a command pool can't be used by two threads at once
		std::vector<vk::raii::CommandPool> recording_command_pools_ {};

		// Indexed by recording thread, then by frame in flight
		std::vector<std::vector<vk::raii::CommandBuffer>> secondary_command_buffers_ {};

		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> particle_command_buffers_ {};

		Mesh mesh_ {};
		Buffer vertex_buffer_ {};

		// Vertices shared by several triangles are stored only once
		Buffer index_buffer_ {};

		// Read from the texture file, which can store its texels in any format
		vk::Format texture_format_ {};

		Image texture_image_ {};
		vk::raii::ImageView texture_image_view_ { nullptr };
		vk::raii::Sampler texture_sampler_ { nullptr };

		// Camera matrices, one per frame in flight so the CPU never overwrites one in use
		std::vector<Buffer> uniform_buffers_ {};

		// Kept mapped, since they're written every frame
		std::vector<void*> mapped_uniform_buffers_ {};

		vk::raii::DescriptorPool descriptor_pool_ { nullptr };

		// One per frame in flight, because each points to its frame's uniform buffer
		std::vector<vk::raii::DescriptorSet> descriptor_sets_ {};

		// Signaled by acquireNextImage, waited on by the submission, one per frame in flight
		std::vector<vk::raii::Semaphore> image_available_semaphores_ {};

		// Signaled by the submission, waited on by the CPU, one per frame in flight
		std::vector<vk::raii::Fence> in_flight_fences_ {};

		std::uint32_t frame_index_ {};

		// The objects rotate according to the time elapsed since then
		std::chrono::steady_clock::time_point start_time_ { std::chrono::steady_clock::now() };

		// The particles move according to the time elapsed since then
		std::chrono::steady_clock::time_point previous_frame_time_ {
			std::chrono::steady_clock::now()
		};

		// Declared last, so its threads are joined before anything they record into is destroyed
		WorkerThreads recording_threads_;
	};
}
