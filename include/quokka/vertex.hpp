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

		// The four joints that move it, as indices into the skin's joints, and how much each one
		// does, adding up to 1
		glm::u8vec4 joints;
		glm::vec4 weights;

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
		[[nodiscard]] static constexpr std::array<vk::VertexInputAttributeDescription, 5>
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

			// Read as unsigned integers, not converted to floats
			constexpr vk::VertexInputAttributeDescription joints_attribute {
				.location { 3 },
				.binding { 0 },
				.format { vk::Format::eR8G8B8A8Uint },
				.offset { offsetof(Vertex, joints) }
			};

			constexpr vk::VertexInputAttributeDescription weights_attribute {
				.location { 4 },
				.binding { 0 },
				.format { vk::Format::eR32G32B32A32Sfloat },
				.offset { offsetof(Vertex, weights) }
			};

			return {
				position_attribute, normal_attribute, texture_coordinates_attribute,
				joints_attribute, weights_attribute
			};
		}
	};
}
