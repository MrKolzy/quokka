#include "quokka/shader_module.hpp"

#include <cstddef> // std::size_t
#include <fstream> // std::ifstream, std::streamsize
#include <stdexcept> // std::runtime_error
#include <string> // operator+ for std::string

namespace quokka
{
	std::vector<std::uint32_t> read_shader_file(const std::filesystem::path& path)
	{
		// ate starts at the end to get the size, binary skips text conversions
		std::ifstream file { path, std::ios::ate | std::ios::binary };
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to open shader file: " + path.string());
		}

		const auto file_size { static_cast<std::size_t>(file.tellg()) };

		// SPIR-V is a stream of 32-bit words, so an empty file or a partial word means it's invalid
		if (file_size == 0 || file_size % sizeof(std::uint32_t) != 0)
		{
			throw std::runtime_error("Invalid SPIR-V file size: " + path.string());
		}

		// Parentheses because braces would call the initializer_list constructor
		std::vector<std::uint32_t> code(file_size / sizeof(std::uint32_t));

		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(file_size));

		if (!file)
		{
			throw std::runtime_error("Failed to read shader file: " + path.string());
		}

		return code;
	}

	vk::raii::ShaderModule create_shader_module(
		const vk::raii::Device& device,
		const std::vector<std::uint32_t>& code)
	{
		const vk::ShaderModuleCreateInfo create_info {
			// Size in bytes, not in words
			.codeSize { code.size() * sizeof(std::uint32_t) },
			.pCode { code.data() }
		};

		return device.createShaderModule(create_info);
	}
}
