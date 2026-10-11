#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array> // std::array
#include <cstddef> // offsetof

namespace quokka
{
	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 texture_coordinates;

		// Interleaved, so a single binding steps through the buffer one Vertex at a time
		[[nodiscard]] static constexpr vk::VertexInputBindingDescription
			binding_description() noexcept
		{
			return {
				.binding { 0 },
				.stride { sizeof(Vertex) },
				.inputRate { vk::VertexInputRate::eVertex }
			};
		}

		// Locations must match VertexInput in shader.slang
		[[nodiscard]] static constexpr std::array<vk::VertexInputAttributeDescription, 3>
			attribute_descriptions() noexcept
		{
			constexpr vk::VertexInputAttributeDescription position_attribute {
				.location { 0 },
				.binding { 0 },
				.format { vk::Format::eR32G32B32Sfloat },
				.offset { offsetof(Vertex, position) }
			};

			constexpr vk::VertexInputAttributeDescription normal_attribute {
				.location { 1 },
				.binding { 0 },
				.format { vk::Format::eR32G32B32Sfloat },
				.offset { offsetof(Vertex, normal) }
			};

			constexpr vk::VertexInputAttributeDescription texture_coordinates_attribute {
				.location { 2 },
				.binding { 0 },
				.format { vk::Format::eR32G32Sfloat },
				.offset { offsetof(Vertex, texture_coordinates) }
			};

			return { position_attribute, normal_attribute, texture_coordinates_attribute };
		}
	};
}
