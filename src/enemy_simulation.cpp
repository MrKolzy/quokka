#include "quokka/enemy_simulation.hpp"

#include "quokka/barrier.hpp"
#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/shader_module.hpp"

#include <array> // std::array
#include <cstddef> // offsetof
#include <cstdint> // std::uint32_t
#include <limits> // std::numeric_limits
#include <span> // std::as_bytes, std::span
#include <utility> // std::move
#include <vector> // std::vector

namespace quokka
{
	namespace
	{
		struct SimulationConstants
		{
			// Must match SimulationConstants in enemy_simulation.slang
			glm::vec2 target;
			float delta_seconds;
			std::uint32_t enemy_count;
			glm::vec2 aim_point;
			float time;
			std::uint32_t casts;

			// Minimum and maximum, from the abilities so their descriptions match what they do
			glm::vec2 strike_damage;
			glm::vec2 blast_damage;
			std::uint32_t critical_chance;

			// The shader's struct holds float2 members, so its size is a multiple of 8
			std::uint32_t padding;
		};

		[[nodiscard]] glm::vec2 damage_range(Ability ability) noexcept
		{
			return { ability_info(ability).min_damage, ability_info(ability).max_damage };
		}

		// Cast flags, must match blast_cast and test_cast in enemy_simulation.slang
		// The strike has no flag, since it's a dispatch of its own
		constexpr std::uint32_t blast_cast { 1 };
		constexpr std::uint32_t test_cast { 2 };

		struct CombatState
		{
			// Must match CombatState in enemy.slang
			// The first two are cleared before every update, the rest are kept between frames
			std::uint32_t target_key;
			std::uint32_t frame_damage;
			std::uint32_t spawned_number_count;
			std::uint32_t total_damage;
			std::uint32_t previous_total_damage;
			float last_total_hit_time;
		};

		// Must match workgroup_size in enemy_simulation.slang
		constexpr std::uint32_t workgroup_size { 64 };

		constexpr vk::DeviceSize states_size { sizeof(EnemyState) * constants::enemy_count };

		// Must match no_target in enemy.slang
		constexpr std::uint32_t no_target { 0xFFFFFFFF };

		// The target key keeps the index in 16 bits, and the largest key means no target
		static_assert(constants::enemy_count < 0xFFFF);

		// The abilities in abilities.hpp are balanced around it
		constexpr float enemy_health { 1000.0f };

		// The smallest side whose square holds the count, so the last row may be incomplete
		[[nodiscard]] constexpr std::uint32_t square_side(std::uint32_t count) noexcept
		{
			std::uint32_t side { 1 };
			while (side * side < count)
			{
				++side;
			}

			return side;
		}

		constexpr std::uint32_t grid_side { square_side(constants::enemy_count) };

		// Between the centers of neighbors at the start, a bit more than the width of the model
		constexpr float grid_spacing { 2.0f };

		// A square grid on the ground centered at the origin, each enemy turned differently
		[[nodiscard]] std::vector<EnemyState> initial_grid()
		{
			const float grid_offset { static_cast<float>(grid_side - 1) * grid_spacing / 2.0f };

			std::vector<EnemyState> states {};
			states.reserve(constants::enemy_count);

			for (std::uint32_t index { 0 }; index < constants::enemy_count; ++index)
			{
				const std::uint32_t row { index / grid_side };
				const std::uint32_t column { index % grid_side };

				states.push_back({
					.position {
						static_cast<float>(column) * grid_spacing - grid_offset,
						static_cast<float>(row) * grid_spacing - grid_offset
					},
					.yaw { static_cast<float>(index) * glm::radians(40.0f) },
					.is_chasing { 0 },
					.health { enemy_health },
					// Never hit, so there's no flash
					.last_hit_time { -std::numeric_limits<float>::infinity() }
				});
			}

			return states;
		}

		vk::raii::DescriptorSetLayout create_descriptor_set_layout(const vk::raii::Device& device)
		{
			// Must match enemies in enemy_simulation.slang
			constexpr vk::DescriptorSetLayoutBinding state_binding {
				.binding { 0 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.descriptorCount { 1 },
				.stageFlags { vk::ShaderStageFlagBits::eCompute }
			};

			// Must match previous_enemies in enemy_simulation.slang
			constexpr vk::DescriptorSetLayoutBinding previous_binding {
				.binding { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.descriptorCount { 1 },
				.stageFlags { vk::ShaderStageFlagBits::eCompute }
			};

			// Must match combat in enemy_simulation.slang
			constexpr vk::DescriptorSetLayoutBinding combat_binding {
				.binding { 2 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.descriptorCount { 1 },
				.stageFlags { vk::ShaderStageFlagBits::eCompute }
			};

			// Must match damage_numbers in enemy_simulation.slang
			constexpr vk::DescriptorSetLayoutBinding damage_number_binding {
				.binding { 3 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.descriptorCount { 1 },
				.stageFlags { vk::ShaderStageFlagBits::eCompute }
			};

			constexpr std::array bindings {
				state_binding, previous_binding, combat_binding, damage_number_binding
			};

			const vk::DescriptorSetLayoutCreateInfo create_info {
				.bindingCount { static_cast<std::uint32_t>(bindings.size()) },
				.pBindings { bindings.data() }
			};

			return device.createDescriptorSetLayout(create_info);
		}

		vk::raii::PipelineLayout create_pipeline_layout(
			const vk::raii::Device& device,
			const vk::raii::DescriptorSetLayout& descriptor_set_layout)
		{
			// Pushed every update, since they change every frame
			constexpr vk::PushConstantRange constants_range {
				.stageFlags { vk::ShaderStageFlagBits::eCompute },
				.offset { 0 },
				.size { sizeof(SimulationConstants) }
			};

			const vk::PipelineLayoutCreateInfo pipeline_layout_info {
				.setLayoutCount { 1 },
				.pSetLayouts { &*descriptor_set_layout },
				.pushConstantRangeCount { 1 },
				.pPushConstantRanges { &constants_range }
			};

			return device.createPipelineLayout(pipeline_layout_info);
		}

		vk::raii::Pipeline create_pipeline(
			const vk::raii::Device& device,
			const vk::raii::PipelineLayout& pipeline_layout,
			const char* entry_point)
		{
			const auto shader_code { read_shader_file(constants::enemy_simulation_shader_path) };
			const auto shader_module { create_shader_module(device, shader_code) };

			// Must match the entry points in enemy_simulation.slang and in the slangc command
			const vk::ComputePipelineCreateInfo pipeline_info {
				.stage {
					.stage { vk::ShaderStageFlagBits::eCompute },
					.module { *shader_module },
					.pName { entry_point }
				},
				.layout { *pipeline_layout }
			};

			return device.createComputePipeline(nullptr, pipeline_info);
		}
	}

	// ***

	EnemySimulation::EnemySimulation(const Device& device, const Buffer& damage_number_buffer)
		: descriptor_set_layout_ { create_descriptor_set_layout(device.logical_device()) }
		, pipeline_layout_ {
			create_pipeline_layout(device.logical_device(), descriptor_set_layout_)
		}
		, update_pipeline_ {
			create_pipeline(device.logical_device(), pipeline_layout_, "update_main")
		}
		, strike_pipeline_ {
			create_pipeline(device.logical_device(), pipeline_layout_, "strike_main")
		}
		, tally_pipeline_ {
			create_pipeline(device.logical_device(), pipeline_layout_, "tally_main")
		}
	{
		create_buffers(device);
		create_descriptor_set(device, damage_number_buffer);
	}

	glm::vec2 EnemySimulation::player_start() noexcept
	{
		// Along the diagonal, so the distance to the corner enemy is sqrt 2 times this margin,
		// which must stay above aggro_radius in enemy_simulation.slang
		constexpr float corner_margin { 8.0f };
		constexpr float corner_offset {
			static_cast<float>(grid_side - 1) * grid_spacing / 2.0f + corner_margin
		};

		return { corner_offset, corner_offset };
	}

	void EnemySimulation::prepare_update(
		glm::vec2 target,
		glm::vec2 aim_point,
		float delta_seconds,
		float time_seconds) noexcept
	{
		target_ = target;
		aim_point_ = aim_point;
		delta_seconds_ = delta_seconds;
		time_seconds_ = time_seconds;
	}

	void EnemySimulation::cast(Ability ability) noexcept
	{
		switch (ability)
		{
		case Ability::strike:
			is_strike_cast_ = true;
			break;
		case Ability::blast:
			is_blast_cast_ = true;
			break;
		case Ability::count:
			break;
		}
	}

	void EnemySimulation::cast_test_hits() noexcept
	{
		is_test_cast_ = true;
	}

	void EnemySimulation::reset() noexcept
	{
		should_reset_ = true;
	}

	const Buffer& EnemySimulation::state_buffer() const noexcept
	{
		return states_;
	}

	const Buffer& EnemySimulation::combat_buffer() const noexcept
	{
		return combat_;
	}

	void EnemySimulation::record_update(const vk::raii::CommandBuffer& command_buffer)
	{
		// The previous frame copied, read the copy and wrote the states and the combat state,
		// and its vertex shaders read them, so the new copy and clear wait for all of them
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader |
			vk::PipelineStageFlagBits2::eVertexShader,
			vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite
		);

		// From the starting grid after a reset, so the update starts over from there
		const Buffer& source { should_reset_ ? initial_states_ : states_ };
		should_reset_ = false;

		command_buffer.copyBuffer(
			*source.handle,
			*previous_states_.handle,
			vk::BufferCopy { .srcOffset { 0 }, .dstOffset { 0 }, .size { states_size } }
		);

		// Nobody bid to be the target yet, and the bids keep the smallest key
		// No hits yet either, which add their damage to the frame's
		command_buffer.fillBuffer(
			*combat_.handle,
			offsetof(CombatState, target_key),
			sizeof(CombatState::target_key),
			no_target
		);
		command_buffer.fillBuffer(
			*combat_.handle,
			offsetof(CombatState, frame_damage),
			sizeof(CombatState::frame_damage),
			0
		);

		// The update reads the copy and rewrites the states the earlier work used
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader |
			vk::PipelineStageFlagBits2::eVertexShader,
			vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
		);

		command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *update_pipeline_);

		// Set 0, with no dynamic offsets, kept for the strike since it shares the layout
		command_buffer.bindDescriptorSets(
			vk::PipelineBindPoint::eCompute,
			*pipeline_layout_,
			0,
			*descriptor_set_,
			{}
		);

		const SimulationConstants simulation_constants {
			.target { target_ },
			.delta_seconds { delta_seconds_ },
			.enemy_count { constants::enemy_count },
			.aim_point { aim_point_ },
			.time { time_seconds_ },
			.casts { (is_blast_cast_ ? blast_cast : 0) | (is_test_cast_ ? test_cast : 0) },
			.strike_damage { damage_range(Ability::strike) },
			.blast_damage { damage_range(Ability::blast) },
			.critical_chance { critical_chance }
		};
		command_buffer.pushConstants<SimulationConstants>(
			*pipeline_layout_,
			vk::ShaderStageFlagBits::eCompute,
			0,
			simulation_constants
		);

		// One thread per enemy, rounded up to whole workgroups
		constexpr std::uint32_t workgroup_count {
			(constants::enemy_count + workgroup_size - 1) / workgroup_size
		};
		command_buffer.dispatch(workgroup_count, 1, 1);

		// Only once every enemy has moved and bid, so the strike reads the final target
		// The push constants and the set stay bound, since the layout is the same
		if (is_strike_cast_)
		{
			record_compute_to_compute_barrier(command_buffer);
			command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *strike_pipeline_);
			command_buffer.dispatch(1, 1, 1);
		}

		// Without a cast nothing was hit, so there's nothing to add to the total
		if (is_blast_cast_ || is_strike_cast_ || is_test_cast_)
		{
			record_compute_to_compute_barrier(command_buffer);
			command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *tally_pipeline_);
			command_buffer.dispatch(1, 1, 1);
		}

		is_blast_cast_ = false;
		is_strike_cast_ = false;
		is_test_cast_ = false;

		// The scene and the damage numbers read the new states, the numbers spawned and the total
		// in their vertex shaders
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eVertexShader,
			vk::AccessFlagBits2::eShaderStorageRead
		);
	}

	void EnemySimulation::create_buffers(const Device& device)
	{
		// Only for these uploads, which happen once
		const vk::CommandPoolCreateInfo pool_info {
			.flags { vk::CommandPoolCreateFlagBits::eTransient },
			.queueFamilyIndex { device.queue_family_index() }
		};

		const auto command_pool { device.logical_device().createCommandPool(pool_info) };

		const std::vector<EnemyState> grid { initial_grid() };
		const auto grid_bytes { std::as_bytes(std::span { grid }) };

		// Only ever copied from
		initial_states_ = create_device_local_buffer(
			device,
			command_pool,
			grid_bytes,
			vk::BufferUsageFlagBits::eTransferSrc
		);

		// Updated by the compute shader, read by the vertex shaders and copied every frame
		states_ = create_device_local_buffer(
			device,
			command_pool,
			grid_bytes,
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc
		);

		// Filled by the first update's copy, so it needs no contents yet
		previous_states_ = create_buffer(
			device,
			states_size,
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		// No numbers spawned and no total yet, which no hit ever reached
		// The fields cleared every update are filled again before they're read
		const CombatState initial_combat {
			.target_key { no_target },
			.frame_damage { 0 },
			.spawned_number_count { 0 },
			.total_damage { 0 },
			.previous_total_damage { 0 },
			.last_total_hit_time { -std::numeric_limits<float>::infinity() }
		};

		// Also a fill destination, which create_device_local_buffer allows for its upload
		combat_ = create_device_local_buffer(
			device,
			command_pool,
			std::as_bytes(std::span { &initial_combat, 1 }),
			vk::BufferUsageFlagBits::eStorageBuffer
		);
	}

	void EnemySimulation::record_compute_to_compute_barrier(
		const vk::raii::CommandBuffer& command_buffer) const
	{
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
		);
	}

	void EnemySimulation::create_descriptor_set(
		const Device& device,
		const Buffer& damage_number_buffer)
	{
		constexpr vk::DescriptorPoolSize pool_size {
			.type { vk::DescriptorType::eStorageBuffer },
			.descriptorCount { 4 }
		};

		const vk::DescriptorPoolCreateInfo pool_info {
			// vk::raii::DescriptorSet frees its set when destroyed, which requires this flag
			.flags { vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet },
			.maxSets { 1 },
			.poolSizeCount { 1 },
			.pPoolSizes { &pool_size }
		};

		descriptor_pool_ = device.logical_device().createDescriptorPool(pool_info);

		const vk::DescriptorSetAllocateInfo allocate_info {
			.descriptorPool { *descriptor_pool_ },
			.descriptorSetCount { 1 },
			.pSetLayouts { &*descriptor_set_layout_ }
		};

		descriptor_set_ = std::move(device.logical_device().allocateDescriptorSets(allocate_info)[0]);

		const vk::DescriptorBufferInfo state_info {
			.buffer { *states_.handle },
			.offset { 0 },
			.range { vk::WholeSize }
		};

		const vk::DescriptorBufferInfo previous_info {
			.buffer { *previous_states_.handle },
			.offset { 0 },
			.range { vk::WholeSize }
		};

		const vk::DescriptorBufferInfo combat_info {
			.buffer { *combat_.handle },
			.offset { 0 },
			.range { vk::WholeSize }
		};

		const vk::DescriptorBufferInfo damage_number_info {
			.buffer { *damage_number_buffer.handle },
			.offset { 0 },
			.range { vk::WholeSize }
		};

		const std::array descriptor_writes {
			vk::WriteDescriptorSet {
				.dstSet { *descriptor_set_ },
				// Must match state_binding above
				.dstBinding { 0 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &state_info }
			},
			vk::WriteDescriptorSet {
				.dstSet { *descriptor_set_ },
				// Must match previous_binding above
				.dstBinding { 1 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &previous_info }
			},
			vk::WriteDescriptorSet {
				.dstSet { *descriptor_set_ },
				// Must match combat_binding above
				.dstBinding { 2 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &combat_info }
			},
			vk::WriteDescriptorSet {
				.dstSet { *descriptor_set_ },
				// Must match damage_number_binding above
				.dstBinding { 3 },
				.dstArrayElement { 0 },
				.descriptorCount { 1 },
				.descriptorType { vk::DescriptorType::eStorageBuffer },
				.pBufferInfo { &damage_number_info }
			}
		};

		// No copies from other descriptor sets
		device.logical_device().updateDescriptorSets(descriptor_writes, {});
	}
}
