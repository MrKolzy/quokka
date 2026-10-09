#pragma once

#include <vulkan/vulkan_raii.hpp>

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
	inline constexpr const char* damage_number_shader_path { "shaders/damage_numbers.spv" };
	inline constexpr const char* enemy_simulation_shader_path { "shaders/enemy_simulation.spv" };
	inline constexpr const char* health_bar_shader_path { "shaders/health_bars.spv" };
	inline constexpr const char* enemy_model_path { "models/skeleton_minion.glb" };
	inline constexpr const char* enemy_texture_path { "textures/skeleton.ktx2" };
	inline constexpr const char* player_model_path { "models/knight.glb" };
	inline constexpr const char* player_texture_path { "textures/knight.ktx2" };

	// The KayKit rig both models share, whose files only lend their animations
	inline constexpr const char* general_rig_path { "models/rig_medium_general.glb" };
	inline constexpr const char* movement_rig_path { "models/rig_medium_movement_basic.glb" };

	// Mailbox has low latency without tearing, Immediate tears but has no frame rate cap,
	// which suits benchmarks, and Fifo is vertical sync, which saves power
	inline constexpr vk::PresentModeKHR preferred_present_mode { vk::PresentModeKHR::eMailbox };

	// Samples per pixel for MSAA, lowered if the GPU supports fewer, and 1 disables it
	inline constexpr vk::SampleCountFlagBits max_sample_count { vk::SampleCountFlagBits::e4 };

	// In seconds since the start, for what never happened, so every effect timed from it is over
	// Finite, because shader math on infinities can turn into NaN once the driver optimizes it
	inline constexpr float long_ago { -1.0e6f };

	// Frames the CPU can record ahead of the GPU, more would only add latency
	inline constexpr std::uint32_t max_frames_in_flight { 2 };
	static_assert(max_frames_in_flight > 0);

	// Placeholder enemies, drawn with instancing in a square grid until they can move
	inline constexpr std::uint32_t enemy_count { 1000 };
	static_assert(enemy_count > 0);

	// Threads that record the draws in parallel, 1 to compare against a single thread
	inline constexpr std::uint32_t recording_thread_count { 4 };
	static_assert(recording_thread_count > 0);
}
