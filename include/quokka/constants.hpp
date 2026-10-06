#pragma once

#include <array> // std::array
#include <cstdint> // std::uint32_t

namespace quokka::constants
{
	inline constexpr const char* app_name { "Quokka" };
	inline constexpr int window_width { 800 };
	inline constexpr int window_height { 600 };
	inline constexpr bool is_window_resizable { true };

	inline constexpr std::array validation_layers { "VK_LAYER_KHRONOS_validation" };

	// Only in debug builds, since validation is slow
#ifdef NDEBUG
	inline constexpr bool should_enable_validation_layers { false };
	inline constexpr bool should_print_debug_output { false };
#else
	inline constexpr bool should_enable_validation_layers { true };
	inline constexpr bool should_print_debug_output { true };
#endif

	// In the window title, also in release builds
	inline constexpr bool should_show_frame_stats { true };

	// Relative to the working directory
	inline constexpr const char* shader_path { "shaders/shader.spv" };
	inline constexpr const char* particle_shader_path { "shaders/particles.spv" };
	inline constexpr const char* model_path { "models/viking_room.glb" };
	inline constexpr const char* texture_path { "textures/viking_room.ktx2" };

	// Frames the CPU can record ahead of the GPU, more would only add latency
	inline constexpr std::uint32_t max_frames_in_flight { 2 };
	static_assert(max_frames_in_flight > 0);

	// Model copies per side of the grid, raised to stress draw recording
	inline constexpr std::uint32_t object_grid_side { 10 };
	static_assert(object_grid_side > 0);

	// Threads that record the draws in parallel, 1 to compare against a single thread
	inline constexpr std::uint32_t recording_thread_count { 4 };
	static_assert(recording_thread_count > 0);

	// Simulated on the GPU by a compute shader every frame
	inline constexpr std::uint32_t particle_count { 65536 };
	static_assert(particle_count > 0);
}
