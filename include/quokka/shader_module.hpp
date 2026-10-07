#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint> // std::uint32_t
#include <filesystem> // std::filesystem::path
#include <vector> // std::vector

namespace quokka
{
	// 32-bit words because Vulkan requires 4-byte aligned code
	std::vector<std::uint32_t> read_shader_file(const std::filesystem::path& path);

	// Only needed until the pipelines that use it are created
	vk::raii::ShaderModule create_shader_module(
		const vk::raii::Device& device,
		const std::vector<std::uint32_t>& code);
}
