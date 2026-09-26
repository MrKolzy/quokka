#pragma once

#include <array>

namespace quokka::constants
{
	inline constexpr const char* name { "Quokka" };
	inline constexpr int width { 800 };
	inline constexpr int height { 600 };

	// Validation layers and debug output, only enabled in debug builds
	inline constexpr std::array<const char*, 1> validation_layers { "VK_LAYER_KHRONOS_validation" };
#ifdef NDEBUG
	inline constexpr bool enable_validation_layers { false };
	inline constexpr bool enable_debug_output { false };
#else
	inline constexpr bool enable_validation_layers { true };
	inline constexpr bool enable_debug_output { true };
#endif
}
