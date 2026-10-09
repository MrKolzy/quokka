#pragma once

#include <tiny_gltf.h>

#include <cstddef> // std::size_t
#include <cstring> // std::memcpy
#include <filesystem> // std::filesystem::path
#include <vector> // std::vector

namespace quokka
{
	// A negative or too big glTF index from a malformed file makes vector::at throw
	template <typename T>
	[[nodiscard]] const T& at_index(const std::vector<T>& elements, int index)
	{
		return elements.at(static_cast<std::size_t>(index));
	}

	struct AccessorData
	{
		const unsigned char* first_element;
		std::size_t stride;
		std::size_t count;
	};

	// Also checks that every element fits in the buffer
	[[nodiscard]] AccessorData accessor_data(
		const tinygltf::Model& model,
		const tinygltf::Accessor& accessor,
		std::size_t element_size);

	// memcpy, since reading the bytes through a float pointer breaks C++ aliasing rules
	template <typename T>
	[[nodiscard]] T read_element(const AccessorData& data, std::size_t index)
	{
		T element {};
		std::memcpy(&element, data.first_element + index * data.stride, sizeof(element));

		return element;
	}

	// A .gltf or .glb file, with its images left undecoded since textures come from KTX2 files
	[[nodiscard]] tinygltf::Model read_gltf(const std::filesystem::path& path);
}
