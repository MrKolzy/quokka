#include "quokka/enemy_simulation.hpp"

#include "quokka/barrier.hpp"
#include "quokka/camera.hpp"
#include "quokka/constants.hpp"
#include "quokka/device.hpp"
#include "quokka/shader_module.hpp"

#include <array> // std::array
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
		};

		// Must match workgroup_size in enemy_simulation.slang
		constexpr std::uint32_t workgroup_size { 64 };

		constexpr vk::DeviceSize states_size { sizeof(EnemyState) * constants::enemy_count };

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
					.is_chasing { 0 }
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

			constexpr std::array bindings { state_binding, previous_binding };

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
			const vk::raii::PipelineLayout& pipeline_layout)
		{
			const auto shader_code { read_shader_file(constants::enemy_simulation_shader_path) };
			const auto shader_module { create_shader_module(device, shader_code) };

			// Must match the entry point in enemy_simulation.slang and in the slangc command
			const vk::ComputePipelineCreateInfo pipeline_info {
				.stage {
					.stage { vk::ShaderStageFlagBits::eCompute },
					.module { *shader_module },
					.pName { "update_main" }
				},
				.layout { *pipeline_layout }
			};

			return device.createComputePipeline(nullptr, pipeline_info);
		}
	}

	// ***

	EnemySimulation::EnemySimulation(const Device& device)
		: descriptor_set_layout_ { create_descriptor_set_layout(device.logical_device()) }
		, pipeline_layout_ {
			create_pipeline_layout(device.logical_device(), descriptor_set_layout_)
		}
		, pipeline_ { create_pipeline(device.logical_device(), pipeline_layout_) }
	{
		create_buffers(device);
		create_descriptor_set(device);
	}

	Camera EnemySimulation::initial_camera() noexcept
	{
		// Back and above the grid, far enough to see all of it, whatever its size
		constexpr float grid_size { static_cast<float>(grid_side) * grid_spacing };
		constexpr glm::vec3 camera_position {
			0.0f, grid_size * 0.7f + 1.0f, grid_size * 1.3f + 2.0f
		};

		constexpr glm::vec3 grid_center { 0.0f, 0.0f, 0.0f };

		return { camera_position, grid_center };
	}

	void EnemySimulation::prepare_update(glm::vec2 target, float delta_seconds) noexcept
	{
		target_ = target;
		delta_seconds_ = delta_seconds;
	}

	void EnemySimulation::reset() noexcept
	{
		should_reset_ = true;
	}

	const Buffer& EnemySimulation::state_buffer() const noexcept
	{
		return states_;
	}

	void EnemySimulation::record_update(const vk::raii::CommandBuffer& command_buffer)
	{
		// The previous frame copied, read the copy and wrote the states, and its vertex shaders
		// read the states, so the new copy waits for all of them
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

		// The update reads the copy and rewrites the states the earlier work used
		memory_barrier(
			command_buffer,
			vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader |
			vk::PipelineStageFlagBits2::eVertexShader,
			vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
		);

		command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline_);

		// Set 0, with no dynamic offsets
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
			.enemy_count { constants::enemy_count }
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

		// The scene and the damage numbers read the new states in their vertex shaders
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
	}

	void EnemySimulation::create_descriptor_set(const Device& device)
	{
		constexpr vk::DescriptorPoolSize pool_size {
			.type { vk::DescriptorType::eStorageBuffer },
			.descriptorCount { 2 }
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
			}
		};

		// No copies from other descriptor sets
		device.logical_device().updateDescriptorSets(descriptor_writes, {});
	}
}
