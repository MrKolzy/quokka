#include "quokka/enemy_simulation.hpp"

#include "quokka/barrier.hpp"
#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/level.hpp"
#include "quokka/shader_module.hpp"

#include <array> // std::array
#include <cstddef> // offsetof
#include <cstdint> // std::uint32_t
#include <span> // std::as_bytes, std::span
#include <utility> // std::exchange, std::move
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
			glm::vec2 facing;
			float time;
			std::uint32_t casts;

			// Minimum and maximum, from the abilities so their descriptions match what they do
			glm::vec2 strike_damage;
			glm::vec2 blast_damage;
			std::uint32_t critical_chance;

			// 0 or 1, the enemies stop attacking a dead target
			std::uint32_t is_target_alive;

			// The shader aligns float3 to 16 bytes
			glm::vec2 selection_alignment;
			glm::vec3 selection_origin;
			std::uint32_t selection_mode;
			glm::vec3 selection_direction;
			std::uint32_t selection_padding;

			glm::vec2 quake_center;
			glm::vec2 quake_damage;
			float quake_front;
			float quake_advance;

			// From the level's difficulty
			std::uint32_t attack_damage;
		};

		[[nodiscard]] glm::vec2 damage_range(Ability ability) noexcept
		{
			return { ability_info(ability).min_damage, ability_info(ability).max_damage };
		}

		// Cast flags, must match the cast flags in enemy_simulation.slang
		// The throw has no flag, since it's a dispatch of its own, but its landing has one
		constexpr std::uint32_t blast_cast { 1 };
		constexpr std::uint32_t test_cast { 2 };
		constexpr std::uint32_t hook_cast { 4 };

		struct CombatState
		{
			// Must match CombatState in enemy.slang
			// The first three are cleared before every update, the rest are kept between frames
			std::uint32_t frame_damage;
			std::uint32_t player_damage;
			std::uint32_t frame_hits;
			std::uint32_t spawned_number_count;
			std::uint32_t total_damage;
			std::uint32_t previous_total_damage;
			float last_total_hit_time;
			float hook_time;
			glm::vec2 selected_position;
			std::uint32_t selected_index;
			float selected_health;
			glm::vec2 hook_point;
			std::uint32_t hook_index;
			std::uint32_t alive_count;
		};

		// Three runs of CombatState, for combat feedback, the target panel and the level's progress
		struct Readback
		{
			std::uint32_t player_damage;
			std::uint32_t frame_hits;
			glm::vec2 selected_position;
			std::uint32_t selected_index;
			float selected_health;
			std::uint32_t alive_count;
		};

		static_assert(
			offsetof(CombatState, frame_hits) == offsetof(CombatState, player_damage) + 4);
		static_assert(
			offsetof(CombatState, selected_index) == offsetof(CombatState, selected_position) + 8);
		static_assert(
			offsetof(CombatState, selected_health) == offsetof(CombatState, selected_index) + 4);

		// Within the 128 bytes of push constants every device supports
		static_assert(sizeof(SimulationConstants) == 124);
		static_assert(offsetof(SimulationConstants, quake_center) == 96);
		static_assert(offsetof(SimulationConstants, selection_origin) == 64);
		static_assert(offsetof(SimulationConstants, selection_mode) == 76);
		static_assert(offsetof(SimulationConstants, selection_direction) == 80);
		// The shader aligns float2 to 8 bytes
		static_assert(sizeof(CombatState) == 64);
		static_assert(offsetof(CombatState, selected_position) == 32);
		static_assert(offsetof(CombatState, hook_point) == 48);
		static_assert(offsetof(CombatState, alive_count) == 60);
		static_assert(sizeof(Readback) == 28);

		// Must match workgroup_size in enemy_simulation.slang
		constexpr std::uint32_t workgroup_size { 64 };

		constexpr vk::DeviceSize states_size { sizeof(EnemyState) * constants::enemy_count };

		// Must match no_target in enemy.slang
		constexpr std::uint32_t no_target { 0xFFFFFFFF };

		// The target key keeps the index in 16 bits, and the largest key means no target
		static_assert(constants::enemy_count < 0xFFFF);

		// The abilities in abilities.hpp are balanced around it, must match enemy_max_health
		// in enemy.slang
		constexpr float enemy_health { 1000.0f };

		// Each pack a square grid on the ground around its center, each enemy turned differently,
		// then dead enemies up to the buffer's size, which the level never simulates or draws
		[[nodiscard]] std::vector<EnemyState> level_start_states(const Level& level)
		{
			std::vector<EnemyState> states {};
			states.reserve(constants::enemy_count);

			for (const EnemyPack& pack : level.packs)
			{
				const std::uint32_t side { square_side(pack.count) };
				const float offset { pack_half_size(pack.count) };

				for (std::uint32_t index { 0 }; index < pack.count; ++index)
				{
					const std::uint32_t row { index / side };
					const std::uint32_t column { index % side };
					const auto state_index { static_cast<float>(states.size()) };

					states.push_back({
						.position {
							pack.center.x + static_cast<float>(column) * pack_spacing - offset,
							pack.center.y + static_cast<float>(row) * pack_spacing - offset
						},
						.yaw { state_index * glm::radians(40.0f) },
						.flags { 0 },
						.health { enemy_health },
						// Never hit, so there's no flash
						.last_hit_time { constants::long_ago },
						// Ready to attack as soon as the player is in reach, and never attacked
						.next_attack_time { 0.0f },
						.last_attack_time { constants::long_ago },
						.knockback { 0.0f }
					});
				}
			}

			// Died long ago, so the shaders would show them fully sunk
			states.resize(
				constants::enemy_count,
				EnemyState {
					.position { 0.0f },
					.yaw { 0.0f },
					.flags { 0 },
					.health { 0.0f },
					.last_hit_time { constants::long_ago },
					.next_attack_time { 0.0f },
					.last_attack_time { constants::long_ago },
					.knockback { 0.0f }
				});

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
		, selection_pipeline_ {
			create_pipeline(device.logical_device(), pipeline_layout_, "selection_main")
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

	void EnemySimulation::start_level(std::size_t level_index) noexcept
	{
		level_index_ = level_index;
		should_reset_ = true;
		selection_mode_ = 3;
		selected_enemy_ = {};
		player_damage_ = 0;
		hit_count_ = 0;
		alive_count_ = enemy_count();

		// The frames in flight still update the previous enemies, so their readbacks are ignored
		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			has_readback_[index] = false;
		}
	}

	std::uint32_t EnemySimulation::enemy_count() const noexcept
	{
		return level_enemy_count(levels[level_index_]);
	}

	std::uint32_t EnemySimulation::alive_count() const noexcept
	{
		return alive_count_;
	}

	void EnemySimulation::prepare_update(
		glm::vec2 target,
		glm::vec2 facing,
		bool is_target_alive,
		float delta_seconds,
		float time_seconds) noexcept
	{
		target_ = target;
		facing_ = facing;
		is_target_alive_ = is_target_alive;
		delta_seconds_ = delta_seconds;
		time_seconds_ = time_seconds;
	}

	void EnemySimulation::select_enemy(glm::vec3 origin, glm::vec3 direction) noexcept
	{
		selection_origin_ = origin;
		selection_direction_ = direction;
		selection_mode_ = 1;
	}

	void EnemySimulation::cycle_enemy() noexcept
	{
		selection_mode_ = 2;
	}

	SelectedEnemy EnemySimulation::selected_enemy() const noexcept
	{
		return selected_enemy_;
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

	void EnemySimulation::land_hook() noexcept
	{
		is_hook_landing_ = true;
	}

	void EnemySimulation::spread_quake(glm::vec2 center, float front, float advance) noexcept
	{
		quake_center_ = center;
		quake_front_ = front;
		quake_advance_ += advance;
	}

	void EnemySimulation::cast_test_hits() noexcept
	{
		is_test_cast_ = true;
	}

	const Buffer& EnemySimulation::state_buffer() const noexcept
	{
		return states_;
	}

	const Buffer& EnemySimulation::combat_buffer() const noexcept
	{
		return combat_;
	}

	void EnemySimulation::collect(std::uint32_t frame_index) noexcept
	{
		if (!has_readback_[frame_index])
		{
			return;
		}
		has_readback_[frame_index] = false;

		auto* const readback { static_cast<Readback*>(mapped_readbacks_[frame_index]) };
		player_damage_ += readback->player_damage;
		hit_count_ += readback->frame_hits;
		selected_enemy_ = {
			readback->selected_index, readback->selected_health, readback->selected_position
		};
		alive_count_ = readback->alive_count;

		// A frame that returns before recording leaves its copy unwritten, and the next wait
		// on the same fence would otherwise count it twice
		*readback = Readback {};
	}

	std::uint32_t EnemySimulation::take_player_damage() noexcept
	{
		return std::exchange(player_damage_, 0u);
	}

	std::uint32_t EnemySimulation::take_hit_count() noexcept
	{
		return std::exchange(hit_count_, 0u);
	}

	void EnemySimulation::record_update(
		const vk::raii::CommandBuffer& command_buffer,
		std::uint32_t frame_index)
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

		// From where the level starts after start_level, so the update starts over from there
		const Buffer& source { should_reset_ ? level_states_[level_index_] : states_ };
		should_reset_ = false;

		command_buffer.copyBuffer(
			*source.handle,
			*previous_states_.handle,
			vk::BufferCopy { .srcOffset { 0 }, .dstOffset { 0 }, .size { states_size } }
		);

		// No hits or attacks yet, which add their damage to the frame's
		command_buffer.fillBuffer(
			*combat_.handle,
			offsetof(CombatState, frame_damage),
			sizeof(CombatState::frame_damage) + sizeof(CombatState::player_damage) +
				sizeof(CombatState::frame_hits),
			0
		);

		// Every living enemy adds itself during the update
		command_buffer.fillBuffer(
			*combat_.handle,
			offsetof(CombatState, alive_count),
			sizeof(CombatState::alive_count),
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

		const Level& level { levels[level_index_] };
		const std::uint32_t enemy_count { level_enemy_count(level) };

		const SimulationConstants simulation_constants {
			.target { target_ },
			.delta_seconds { delta_seconds_ },
			.enemy_count { enemy_count },
			.facing { facing_ },
			.time { time_seconds_ },
			.casts {
				(is_blast_cast_ ? blast_cast : 0) | (is_test_cast_ ? test_cast : 0) |
				(is_hook_landing_ ? hook_cast : 0)
			},
			.strike_damage { damage_range(Ability::strike) },
			.blast_damage { damage_range(Ability::blast) },
			.critical_chance { critical_chance },
			.is_target_alive { is_target_alive_ ? 1u : 0u },
			.selection_alignment { 0.0f },
			.selection_origin { selection_origin_ },
			.selection_mode { selection_mode_ },
			.selection_direction { selection_direction_ },
			.selection_padding { 0 },
			.quake_center { quake_center_ },
			.quake_damage { quake_min_damage, quake_max_damage },
			.quake_front { quake_front_ },
			.quake_advance { quake_advance_ },
			.attack_damage { enemy_attack_damage(level.difficulty) }
		};
		command_buffer.pushConstants<SimulationConstants>(
			*pipeline_layout_,
			vk::ShaderStageFlagBits::eCompute,
			0,
			simulation_constants
		);

		// One thread per enemy, rounded up to whole workgroups
		const std::uint32_t workgroup_count {
			(enemy_count + workgroup_size - 1) / workgroup_size
		};
		command_buffer.dispatch(workgroup_count, 1, 1);

		record_compute_to_compute_barrier(command_buffer);
		command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *selection_pipeline_);
		command_buffer.dispatch(1, 1, 1);
		selection_mode_ = 0;

		// Only once every enemy has moved and bid, so the strike reads the final target
		// The push constants and the set stay bound, since the layout is the same
		if (is_strike_cast_)
		{
			record_compute_to_compute_barrier(command_buffer);
			command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *strike_pipeline_);
			command_buffer.dispatch(1, 1, 1);
		}

		// Also refreshes the selected health after the strike, even on frames without a hit
		record_compute_to_compute_barrier(command_buffer);
		command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *tally_pipeline_);
		command_buffer.dispatch(1, 1, 1);

		is_blast_cast_ = false;
		is_strike_cast_ = false;
		is_test_cast_ = false;
		is_hook_landing_ = false;
		quake_advance_ = 0.0f;

		// The scene and the damage numbers read the new states, the numbers spawned and the total
		// in their vertex shaders, and the copy below reads the damage dealt to the player
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eTransferRead
		);

		const std::array readback_regions {
			vk::BufferCopy {
				.srcOffset { offsetof(CombatState, player_damage) },
				.dstOffset { 0 },
				.size { 2 * sizeof(std::uint32_t) }
			},
			vk::BufferCopy {
				.srcOffset { offsetof(CombatState, selected_position) },
				.dstOffset { offsetof(Readback, selected_position) },
				.size { sizeof(glm::vec2) + sizeof(std::uint32_t) + sizeof(float) }
			},
			vk::BufferCopy {
				.srcOffset { offsetof(CombatState, alive_count) },
				.dstOffset { offsetof(Readback, alive_count) },
				.size { sizeof(std::uint32_t) }
			}
		};
		command_buffer.copyBuffer(
			*combat_.handle,
			*readback_buffers_[frame_index].handle,
			readback_regions
		);
		has_readback_[frame_index] = true;

		// Read by collect once this frame's fence is signaled, which also needs this barrier
		// to make the copy visible to the host
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite,
			vk::PipelineStageFlagBits2::eHost,
			vk::AccessFlagBits2::eHostRead
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

		// Only ever copied from
		level_states_.reserve(levels.size());
		for (const Level& level : levels)
		{
			const std::vector<EnemyState> start_states { level_start_states(level) };

			level_states_.push_back(create_device_local_buffer(
				device,
				command_pool,
				std::as_bytes(std::span { start_states }),
				vk::BufferUsageFlagBits::eTransferSrc
			));
		}

		// Updated by the compute shader, read by the vertex shaders and copied every frame
		// It starts as the first level, which is where the game starts
		const std::vector<EnemyState> first_states { level_start_states(levels[level_index_]) };
		states_ = create_device_local_buffer(
			device,
			command_pool,
			std::as_bytes(std::span { first_states }),
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc
		);
		alive_count_ = enemy_count();

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
			.frame_damage { 0 },
			.player_damage { 0 },
			.frame_hits { 0 },
			.spawned_number_count { 0 },
			.total_damage { 0 },
			.previous_total_damage { 0 },
			.last_total_hit_time { constants::long_ago },
			.hook_time { constants::long_ago },
			.selected_position { 0.0f },
			.selected_index { no_target },
			.selected_health { 0.0f },
			.hook_point { 0.0f },
			.hook_index { no_target },
			.alive_count { 0 }
		};

		// Also a fill destination, which create_device_local_buffer allows for its upload,
		// and a copy source for the damage dealt to the player
		combat_ = create_device_local_buffer(
			device,
			command_pool,
			std::as_bytes(std::span { &initial_combat, 1 }),
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc
		);

		// Host visible to map them, host coherent so the reads need no explicit invalidation
		constexpr vk::MemoryPropertyFlags readback_properties {
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		};

		has_readback_.resize(constants::max_frames_in_flight, false);
		readback_buffers_.reserve(constants::max_frames_in_flight);
		mapped_readbacks_.reserve(constants::max_frames_in_flight);

		for (std::uint32_t index { 0 }; index < constants::max_frames_in_flight; ++index)
		{
			readback_buffers_.push_back(create_buffer(
				device,
				sizeof(Readback),
				vk::BufferUsageFlagBits::eTransferDst,
				readback_properties
			));

			// Never unmapped explicitly, freeing the memory unmaps it
			mapped_readbacks_.push_back(readback_buffers_.back().memory.mapMemory(
				0,
				sizeof(Readback)));

			// The first collect of each frame in flight comes before any copy into it
			*static_cast<Readback*>(mapped_readbacks_.back()) = Readback {};
		}
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
