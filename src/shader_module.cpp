#include "quokka/shader_module.hpp"

#include <cstddef> // std::size_t
#include <fstream>
#include <stdexcept> // std::runtime_error
#include <string>

namespace quokka
{
	std::vector<std::uint32_t> read_shader_file(const std::filesystem::path& path)
	{
		// Start at the end (ate) to get the file size, and read raw bytes without text conversions (binary)
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

		// Parentheses, not braces: this is the count constructor
		std::vector<std::uint32_t> code(file_size / sizeof(std::uint32_t));

		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(file_size));

		if (!file)
		{
			throw std::runtime_error("Failed to read shader file: " + path.string());
		}

		return code;
	}

	vk::raii::ShaderModule create_shader_module(const vk::raii::Device& device, const std::vector<std::uint32_t>& code)
	{
		const vk::ShaderModuleCreateInfo create_info {
			// Size in bytes, not in words
			.codeSize { code.size() * sizeof(std::uint32_t) },
			.pCode { code.data() }
		};

		return device.createShaderModule(create_info);
	}
}
