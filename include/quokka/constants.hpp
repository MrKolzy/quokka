#pragma once

#include <array>
#include <cstdint> // std::uint32_t

namespace quokka::constants
{
	inline constexpr const char* app_name { "Quokka" };
	inline constexpr int window_width { 800 };
	inline constexpr int window_height { 600 };
	inline constexpr bool is_window_resizable { true };

	// Validation layers and debug output, only enabled in debug builds
	inline constexpr std::array validation_layers { "VK_LAYER_KHRONOS_validation" };
#ifdef NDEBUG
	inline constexpr bool should_enable_validation_layers { false };
	inline constexpr bool should_print_debug_output { false };
#else
	inline constexpr bool should_enable_validation_layers { true };
	inline constexpr bool should_print_debug_output { true };
#endif

	// Compiled SPIR-V shaders, relative to the working directory
	inline constexpr const char* shader_path { "shaders/shader.spv" };

	// Frames the CPU can record while the GPU is still working on previous ones
	// Not higher, so the CPU doesn't get too far ahead of the GPU and add latency
	inline constexpr std::uint32_t max_frames_in_flight { 2 };
}
