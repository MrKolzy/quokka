#pragma once

#include "quokka/buffer.hpp"
#include "quokka/model.hpp"
#include "quokka/worker_threads.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <vector> // std::vector

namespace quokka
{
	class Camera;
	class DamageNumbers;
	class Device;
	struct DirectionalLight;
	class EnemySimulation;
	class GpuTimer;
	class GraphicsPipeline;
	class Gui;
	class HealthBars;
	class Player;
	class Swapchain;

	class Renderer
	{
	public:
		Renderer(
			const Device& device,
			const Swapchain& swapchain,
			const GraphicsPipeline& graphics_pipeline,
			GpuTimer& gpu_timer,
			const Gui& gui,
			const DamageNumbers& damage_numbers,
			const HealthBars& health_bars,
			EnemySimulation& enemy_simulation);
		~Renderer();

		// recording_threads_ can't be copied or moved, so neither can it
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
		Renderer& operator=(Renderer&&) = delete;

		// Returns true when the swapchain must be recreated before the next frame
		// The time is in seconds since the start, for the animations computed on the GPU
		[[nodiscard]] bool draw_frame(
			const Camera& camera,
			const DirectionalLight& light,
			const Player& player,
			float time_seconds);

	private:
		void create_command_pool();
		void create_command_buffers();
		void create_secondary_command_buffers();
		void create_texture_sampler();
		void create_uniform_buffers();
		void create_descriptor_pool();
		void create_descriptor_sets();
		[[nodiscard]] vk::raii::DescriptorSet create_material_set(const Model& model) const;
		void create_frame_sync_objects();

		void update_uniform_buffer(
			void* mapped_memory,
			const Camera& camera,
			const DirectionalLight& light,
			const Player& player,
			float time_seconds) const;
		void record_command_buffer(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			std::uint32_t image_index,
			const Player& player,
			float time_seconds);

		// The health bars, the damage numbers and the interface, over the finished scene
		// Recorded every frame, since the game's interface is always on screen
		void record_overlay(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			std::uint32_t image_index,
			float time_seconds) const;

		// Called from several recording threads at once, so they must only read shared state
		void begin_secondary_command_buffer(const vk::raii::CommandBuffer& command_buffer) const;
		void record_object_draws(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			std::uint32_t first_object,
			std::uint32_t object_count) const;

		// The model's vertices and indices, and its material set together with the frame's set
		void bind_model(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& frame_set,
			const Model& model,
			const vk::raii::DescriptorSet& material_set) const;

		// The player and the ground, recorded by the main thread so it has its own command buffers
		void record_main_thread_draws(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			const Player& player,
			float time_seconds) const;

		const Device& device_;
		const Swapchain& swapchain_;
		const GraphicsPipeline& graphics_pipeline_;
		GpuTimer& gpu_timer_;
		const Gui& gui_;
		const DamageNumbers& damage_numbers_;
		const HealthBars& health_bars_;
		EnemySimulation& enemy_simulation_;

		// Each one is destroyed before the object it came from, so don't reorder
		vk::raii::CommandPool command_pool_ { nullptr };

		// One per frame in flight
		std::vector<vk::raii::CommandBuffer> primary_command_buffers_ {};

		// One per recording thread, because a command pool can't be used by two threads at once
		std::vector<vk::raii::CommandPool> recording_command_pools_ {};

		// Indexed by recording thread, then by frame in flight
		std::vector<std::vector<vk::raii::CommandBuffer>> secondary_command_buffers_ {};

		// One per frame in flight, from command_pool_ because the main thread records them
		std::vector<vk::raii::CommandBuffer> main_thread_command_buffers_ {};

		Model enemy_model_ {};
		Model player_model_ {};

		// Shared by every texture, since they're all sampled the same way
		vk::raii::Sampler texture_sampler_ { nullptr };

		// Camera and light, one per frame in flight so the CPU never overwrites one in use
		std::vector<Buffer> uniform_buffers_ {};

		// Kept mapped, since they're written every frame
		std::vector<void*> mapped_uniform_buffers_ {};

		vk::raii::DescriptorPool descriptor_pool_ { nullptr };

		// One per frame in flight, because each points to its frame's uniform buffer
		std::vector<vk::raii::DescriptorSet> descriptor_sets_ {};

		// One per model, shared by every frame since textures are never written after loading
		vk::raii::DescriptorSet enemy_material_set_ { nullptr };
		vk::raii::DescriptorSet player_material_set_ { nullptr };

		// Signaled by acquireNextImage, waited on by the submission, one per frame in flight
		std::vector<vk::raii::Semaphore> image_available_semaphores_ {};

		// Signaled by the submission, waited on by the CPU, one per frame in flight
		std::vector<vk::raii::Fence> in_flight_fences_ {};

		std::uint32_t frame_index_ {};

		// Declared last, so its threads are joined before anything they record into is destroyed
		WorkerThreads recording_threads_;
	};
}
