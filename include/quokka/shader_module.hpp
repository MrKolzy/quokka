#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <filesystem>
#include <vector>

namespace quokka
{
	// Reads a compiled SPIR-V file as 32-bit words, which also guarantees the alignment Vulkan expects
	std::vector<std::uint32_t> read_shader_file(const std::filesystem::path& path);

	// Wraps SPIR-V code in a shader module, only needed until the pipeline is created
	vk::raii::ShaderModule create_shader_module(const vk::raii::Device& device, const std::vector<std::uint32_t>& code);
}
