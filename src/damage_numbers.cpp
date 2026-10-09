#include "quokka/damage_numbers.hpp"

#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/overlay_pipeline.hpp"

#include <algorithm> // std::max
#include <span> // std::as_bytes, std::span
#include <vector> // std::vector

namespace quokka
{
	namespace
	{
		// In seconds, must match lifetime in damage_numbers.slang
		constexpr float damage_number_lifetime { 1.0f };

		// Must match max_digits in damage_numbers.slang, with two triangles per digit
		constexpr std::uint32_t max_digits { 6 };
		constexpr std::uint32_t vertices_per_number { max_digits * 6 };

		// Must match label_length plus max_total_digits in damage_numbers.slang
		constexpr std::uint32_t total_glyph_count { 24 };
		constexpr std::uint32_t total_vertex_count { total_glyph_count * 6 };

		// In seconds, must match total_hold_time and total_fade_time in enemy.slang
		constexpr float total_hold_time { 5.0f };
		constexpr float total_fade_time { 1.5f };

		// The stride of the shader's array, so the C++ struct must take the same space
		static_assert(sizeof(DamageNumber) == 16);

		// The scene's frame set layout, so the renderer's frame sets work for both
		vk::raii::PipelineLayout create_pipeline_layout(
			const vk::raii::Device& device,
			const vk::raii::DescriptorSetLayout& descriptor_set_layout)
		{
			// No push constants, the total comes from the combat state the simulation writes
			const vk::PipelineLayoutCreateInfo pipeline_layout_info {
				.setLayoutCount { 1 },
				.pSetLayouts { &*descriptor_set_layout }
			};

			return device.createPipelineLayout(pipeline_layout_info);
		}

		// Every slot starts expired, so nothing shows until the first spawn
		Buffer create_ring_buffer(const Device& device)
		{
			// Only for this upload, which happens once
			const vk::CommandPoolCreateInfo pool_info {
				.flags { vk::CommandPoolCreateFlagBits::eTransient },
				.queueFamilyIndex { device.queue_family_index() }
			};

			const auto command_pool { device.logical_device().createCommandPool(pool_info) };

			// Spawned infinitely long ago, so their age is always past the lifetime
			const std::vector<DamageNumber> expired_numbers(
				DamageNumbers::capacity,
				DamageNumber { .spawn_time { -std::numeric_limits<float>::infinity() } }
			);

			return create_device_local_buffer(
				device,
				command_pool,
				std::as_bytes(std::span { expired_numbers }),
				vk::BufferUsageFlagBits::eStorageBuffer
			);
		}
	}

	// ***

	DamageNumbers::DamageNumbers(
		const Device& device,
		const vk::raii::DescriptorSetLayout& descriptor_set_layout,
		vk::Format color_format)
		: pipeline_layout_ { create_pipeline_layout(device.logical_device(), descriptor_set_layout) }
		, pipeline_ {
			create_overlay_pipeline(
				device.logical_device(),
				pipeline_layout_,
				color_format,
				constants::damage_number_shader_path,
				"vert_main",
				"frag_main")
		}
		, total_pipeline_ {
			create_overlay_pipeline(
				device.logical_device(),
				pipeline_layout_,
				color_format,
				constants::damage_number_shader_path,
				"total_vert_main",
				"frag_main")
		}
		, ring_buffer_ { create_ring_buffer(device) }
	{
	}

	void DamageNumbers::note_cast(float time) noexcept
	{
		last_cast_time_ = std::max(last_cast_time_, time);
	}

	const Buffer& DamageNumbers::buffer() const noexcept
	{
		return ring_buffer_;
	}

	bool DamageNumbers::has_visible_numbers(float time) const noexcept
	{
		// The total stays longer than any number
		return time - last_cast_time_ < total_hold_time + total_fade_time;
	}

	void DamageNumbers::record_draw(
		const vk::raii::CommandBuffer& command_buffer,
		const vk::raii::DescriptorSet& descriptor_set,
		vk::Extent2D extent,
		float time) const
	{
		set_overlay_viewport(command_buffer, extent);

		// Set 0, with no dynamic offsets, kept for both draws since they share the layout
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			*pipeline_layout_,
			0,
			*descriptor_set,
			{}
		);

		// Skipped once every number has expired, since the total stays on screen much longer
		if (time - last_cast_time_ < damage_number_lifetime)
		{
			command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_);

			// Every slot of the ring, and the vertex shader drops expired numbers and unused digits
			command_buffer.draw(vertices_per_number, capacity, 0, 0);
		}

		// Last, so the numbers never cover it
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *total_pipeline_);

		// Every glyph the line can have, and the vertex shader drops the unused ones
		// It hides the whole line when the last hit is too old, or when a cast hit nobody
		command_buffer.draw(total_vertex_count, 1, 0, 0);
	}
}
