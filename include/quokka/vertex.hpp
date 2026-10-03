#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstddef> // offsetof

namespace quokka
{
	// Interleaved: all the attributes of a vertex are stored together in a single buffer
	struct Vertex
	{
		glm::vec2 position;
		glm::vec3 color;
		glm::vec2 texture_coordinates;

		// How to step through the vertex buffer, one Vertex at a time from binding 0
		[[nodiscard]] static constexpr vk::VertexInputBindingDescription binding_description() noexcept
		{
			return {
				.binding { 0 },
				// Bytes between consecutive vertices, including any padding
				.stride { sizeof(Vertex) },
				// Advance per vertex, not per instance
				.inputRate { vk::VertexInputRate::eVertex }
			};
		}

		// How to extract each attribute from a Vertex, the locations must match the shader inputs
		[[nodiscard]] static constexpr std::array<vk::VertexInputAttributeDescription, 3> attribute_descriptions() noexcept
		{
			// float2 in the shader
			constexpr vk::VertexInputAttributeDescription position_attribute {
				.location { 0 },
				.binding { 0 },
				.format { vk::Format::eR32G32Sfloat },
				.offset { offsetof(Vertex, position) }
			};

			// float3 in the shader
			constexpr vk::VertexInputAttributeDescription color_attribute {
				.location { 1 },
				.binding { 0 },
				.format { vk::Format::eR32G32B32Sfloat },
				.offset { offsetof(Vertex, color) }
			};

			// float2 in the shader
			constexpr vk::VertexInputAttributeDescription texture_coordinates_attribute {
				.location { 2 },
				.binding { 0 },
				.format { vk::Format::eR32G32Sfloat },
				.offset { offsetof(Vertex, texture_coordinates) }
			};

			return { position_attribute, color_attribute, texture_coordinates_attribute };
		}
	};
}
