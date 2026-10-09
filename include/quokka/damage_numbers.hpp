#pragma once

#include "quokka/buffer.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <limits> // std::numeric_limits
#include <vector> // std::vector

namespace quokka
{
	class Device;

	struct DamageNumber
	{
		// Must match DamageNumber in damage_numbers.slang
		// The enemy it belongs to, whose position the shader reads every frame
		std::uint32_t enemy_index;

		// In seconds, on the same clock as the time in the frame uniforms
		float spawn_time;

		std::uint32_t value;

		// 0 or 1, since a bool has no fixed size in a shader buffer
		std::uint32_t is_critical;
	};

	class DamageNumbers
	{
	public:
		DamageNumbers(
			const Device& device,
			const vk::raii::DescriptorSetLayout& descriptor_set_layout,
			vk::Format color_format);
		~DamageNumbers() = default;

		// Renderer keeps a reference to it, so it can't be copied or moved
		DamageNumbers(const DamageNumbers&) = delete;
		DamageNumbers& operator=(const DamageNumbers&) = delete;
		DamageNumbers(DamageNumbers&&) = delete;
		DamageNumbers& operator=(DamageNumbers&&) = delete;

		// Shown from the next upload on, and once the ring is full the oldest ones are replaced
		void spawn(const DamageNumber& number);

		// The ring the vertex shader reads, for the descriptor sets
		[[nodiscard]] const Buffer& buffer() const noexcept;

		// Whether a number spawned recently enough to still be on screen at this time
		[[nodiscard]] bool has_visible_numbers(float time) const noexcept;

		// Outside a rendering, because it copies the numbers spawned since the last upload
		void record_upload(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index);

		// Inside a rendering with one sample and no depth, after this frame's upload
		void record_draw(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			vk::Extent2D extent) const;

		// Enough for a hit on 1000 enemies every frame for 16 frames in a row
		static constexpr std::uint32_t capacity { 16384 };

	private:
		vk::raii::PipelineLayout pipeline_layout_;
		vk::raii::Pipeline pipeline_;

		// Every number that may still be on screen, the GPU reads all of it
		Buffer ring_buffer_;

		// Where the next number goes in the ring
		std::uint32_t next_slot_ {};

		// One per frame in flight, written by the CPU and copied into the ring by the GPU
		std::vector<Buffer> staging_buffers_ {};
		std::vector<void*> mapped_staging_buffers_ {};

		// Spawned since the last upload
		std::vector<DamageNumber> pending_numbers_ {};

		float last_spawn_time_ { -std::numeric_limits<float>::infinity() };
	};
}
