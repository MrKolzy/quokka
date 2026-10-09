#pragma once

#include "quokka/buffer.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <limits> // std::numeric_limits

namespace quokka
{
	class Device;

	struct DamageNumber
	{
		// Must match DamageNumber in enemy.slang
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

		// The hits happen on the GPU, so the CPU only knows when they may have, at this time
		// in seconds since the start, which keeps the numbers and the total drawn long enough
		void note_cast(float time) noexcept;

		// The ring the simulation spawns into and the vertex shader reads, for the descriptor sets
		[[nodiscard]] const Buffer& buffer() const noexcept;

		// Whether a number or the total may still be on screen at this time
		[[nodiscard]] bool has_visible_numbers(float time) const noexcept;

		// Inside a rendering with one sample and no depth, after this frame's simulation
		void record_draw(
			const vk::raii::CommandBuffer& command_buffer,
			const vk::raii::DescriptorSet& descriptor_set,
			vk::Extent2D extent,
			float time) const;

		// Enough for a hit on 1000 enemies every frame for 16 frames in a row
		// Must match damage_number_capacity in enemy.slang
		static constexpr std::uint32_t capacity { 16384 };

	private:
		// Shared by the numbers and the total, which only differ in their vertex shader
		vk::raii::PipelineLayout pipeline_layout_;
		vk::raii::Pipeline pipeline_;
		vk::raii::Pipeline total_pipeline_;

		// Every number that may still be on screen, the GPU reads all of it
		Buffer ring_buffer_;

		float last_cast_time_ { -std::numeric_limits<float>::infinity() };
	};
}
