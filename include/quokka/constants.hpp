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

	// Shows the average FPS and frame time in the window title, in every build
	inline constexpr bool should_show_frame_stats { true };

	// Compiled SPIR-V shaders, relative to the working directory
	inline constexpr const char* shader_path { "shaders/shader.spv" };
	inline constexpr const char* particle_shader_path { "shaders/particles.spv" };
	// Model drawn by the renderer and its texture, relative to the working directory
	inline constexpr const char* model_path { "models/viking_room.glb" };
	inline constexpr const char* texture_path { "textures/viking_room.ktx2" };

	// Frames the CPU can record while the GPU is still working on previous ones
	// Not higher, so the CPU doesn't get too far ahead of the GPU and add latency
	inline constexpr std::uint32_t max_frames_in_flight { 2 };

	// Copies of the model drawn in a square grid, this many per side, raised to stress the recording of draw commands
	inline constexpr std::uint32_t object_grid_side { 10 };
	static_assert(object_grid_side > 0);

	// Threads that record the draw commands of each frame in parallel, 1 to compare against recording them on one thread
	inline constexpr std::uint32_t recording_thread_count { 4 };
	static_assert(recording_thread_count > 0);

	// Particles simulated by the compute shader every frame, entirely on the GPU
	inline constexpr std::uint32_t particle_count { 65536 };
	static_assert(particle_count > 0);
}
