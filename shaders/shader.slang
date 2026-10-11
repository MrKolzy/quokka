#include "enemy.slang"
#include "frame.slang"

// Written by the simulation every frame, must match enemy_binding in graphics_pipeline.cpp
[[vk::binding(1, 0)]]
StructuredBuffer<EnemyState> enemies;

// Written by the simulation every frame, must match combat_binding in graphics_pipeline.cpp
[[vk::binding(3, 0)]]
StructuredBuffer<CombatState> combat;

// Image and sampler in one descriptor, in the set of the model being drawn
// Must match texture_binding in graphics_pipeline.cpp
[[vk::binding(0, 1)]]
Sampler2D texture;

// Every frame of every clip, a matrix per joint, must match joint_matrix_binding
// in graphics_pipeline.cpp
[[vk::binding(1, 1)]]
StructuredBuffer<float4x4> joint_matrices;

// Must match ClipInfo in animation.hpp
struct ClipInfo
{
	// Where the clip's first frame starts among the joint matrices
	uint first_matrix;
	uint frame_count;
	uint joint_count;

	// In seconds
	float duration;

	// 0 or 1, the others hold their last frame once they end
	uint is_looping;
};

// Indexed by the clip constants below, must match clip_binding in graphics_pipeline.cpp
[[vk::binding(2, 1)]]
StructuredBuffer<ClipInfo> animation_clips;

// Must match AnimationClip in animation.hpp
static const uint idle_clip = 0;
static const uint walk_clip = 1;
static const uint run_clip = 2;
static const uint attack_clip = 3;
static const uint death_clip = 4;
static const uint jump_clip = 5;
static const uint land_clip = 6;

// Must match animation_frame_rate in animation.hpp
static const float animation_frame_rate = 30.0;

// Must match PlayerConstants in graphics_pipeline.hpp
struct PlayerConstants
{
	// On the ground, as world x and z
	float2 position;

	// The same convention as EnemyState
	float yaw;

	// In seconds since the start, for the flash like EnemyState
	float last_hit_time;

	// One of the clip constants, and how far into it the player is, in seconds
	uint animation_clip;
	float animation_time;

	// In seconds since the start, when the ground last marked how far the sword reaches
	float strike_range_time;
};

[[vk::push_constant]]
ConstantBuffer<PlayerConstants> player;

// Locations follow declaration order and must match Vertex::attribute_descriptions
struct VertexInput
{
	float3 position;
	float3 normal;
	float2 texture_coordinates;
	uint4 joints;
	float4 weights;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float3 world_position;
	float3 normal;
	float2 texture_coordinates;

	// From 0 to 1, the same for the whole model, so they aren't interpolated
	nointerpolation float hit_flash;
	nointerpolation float target_highlight;
};

// Linear, the color that marks what the player aims at, around the targets
static const float3 aim_color = float3(1.0, 0.55, 0.12);

// In seconds, how long a hit turns an enemy white
static const float hit_flash_duration = 0.12;

// A dead enemy plays its death and lies for the delay, then sinks this deep in world units,
// deeper than it's tall, so the ground drawn after it hides it
static const float death_delay = 1.5;
static const float sink_duration = 1.2;
static const float sink_depth = 3.0;

// The enemies' attack swings faster than the clip, which is a throw
static const float enemy_attack_speed = 1.5;

// In world units per second, the walk clip's steps match a chase at 3, so it plays faster with
// a quicker chase and the feet don't slide, must match chase_speed in enemy_simulation.slang
static const float chase_speed = 5.0;
static const float enemy_walk_speed = chase_speed / 3.0;

// In seconds, spreads the enemies over each looping clip so a crowd doesn't move in step
static const float max_animation_offset = 2.0;

// The same rotation as glm::rotate around y, so a quarter turn takes +z to +x
float3 rotate_around_up(float3 value, float angle)
{
	const float cosine = cos(angle);
	const float sine = sin(angle);

	return float3(value.x * cosine + value.z * sine, value.y, value.z * cosine - value.x * sine);
}

// A joint's matrix in the clip, blended between the two frames around the time
float4x4 joint_matrix(ClipInfo clip, uint joint, uint frame, uint next_frame, float blend)
{
	const float4x4 current = joint_matrices[clip.first_matrix + frame * clip.joint_count + joint];
	const float4x4 next = joint_matrices[clip.first_matrix + next_frame * clip.joint_count + joint];

	// Close frames barely differ, so blending the matrices directly looks right
	return lerp(current, next, blend);
}

// The weighted sum of the matrices of the vertex's joints at this moment of the clip
float4x4 skinning_matrix(VertexInput input, uint clip_index, float clip_time)
{
	const ClipInfo clip = animation_clips[clip_index];

	// Looping clips wrap around, the others stop at their last frame
	const float time = clip.is_looping != 0
		? fmod(max(clip_time, 0.0), max(clip.duration, 1.0e-3))
		: clamp(clip_time, 0.0, clip.duration);

	const float frame_position = time * animation_frame_rate;
	const uint frame = min(uint(frame_position), clip.frame_count - 1);
	const uint next_frame = min(frame + 1, clip.frame_count - 1);
	const float blend = frame_position - float(frame);

	float4x4 skinning = float4x4(0.0);
	for (uint influence = 0; influence < 4; ++influence)
	{
		const uint joint = input.joints[influence];
		const float weight = input.weights[influence];
		skinning += weight * joint_matrix(clip, joint, frame, next_frame, blend);
	}

	return skinning;
}

// Posed by the clip, turned to face along the yaw, then moved to its spot on the ground,
// or below it when sinking
// glTF models face +z, which is yaw 0, so they need no extra turn
VertexOutput place_on_ground(
	VertexInput input,
	uint clip,
	float clip_time,
	float2 ground_position,
	float yaw,
	float height)
{
	const float4x4 skinning = skinning_matrix(input, clip, clip_time);
	const float3 posed_position = mul(skinning, float4(input.position, 1.0)).xyz;

	// The joints only rotate and move, so the normal needs no inverse transpose,
	// but blending several of them can shorten it, which the fragment shader fixes
	const float3 posed_normal = mul(skinning, float4(input.normal, 0.0)).xyz;

	const float3 world_position = rotate_around_up(posed_position, yaw) +
		float3(ground_position.x, height, ground_position.y);

	const float4 view_position = mul(frame.view, float4(world_position, 1.0));

	VertexOutput output;
	output.position = mul(frame.projection, view_position);
	output.world_position = world_position;
	output.normal = rotate_around_up(posed_normal, yaw);
	output.texture_coordinates = input.texture_coordinates;
	output.hit_flash = 0.0;
	output.target_highlight = 0.0;

	return output;
}

// SV_VulkanInstanceID includes the first instance of the draw, unlike SV_InstanceID,
// which matters because each recording thread draws its own range of instances
[shader("vertex")]
VertexOutput enemy_vert_main(VertexInput input, uint instance_index : SV_VulkanInstanceID)
{
	const EnemyState enemy = enemies[instance_index];
	const bool is_dead = enemy.health <= 0.0;

	// Never hit is long ago, which saturates to no flash and no sinking
	const float time_since_hit = frame.time - enemy.last_hit_time;
	const float sink_amount = is_dead
		? saturate((time_since_hit - death_delay) / sink_duration)
		: 0.0;

	// Dying, then attacking, then walking, then standing, each from the moment it started,
	// and the looping ones offset differently for each enemy
	const float time_since_attack = frame.time - enemy.last_attack_time;
	const float attack_duration = animation_clips[attack_clip].duration / enemy_attack_speed;
	const float offset = frac(float(instance_index) * 0.618034) * max_animation_offset;

	uint clip = idle_clip;
	float clip_time = frame.time + offset;
	if (is_dead)
	{
		clip = death_clip;
		clip_time = time_since_hit;
	}
	else if (time_since_attack < attack_duration)
	{
		clip = attack_clip;
		clip_time = time_since_attack * enemy_attack_speed;
	}
	else if ((enemy.flags & moving_flag) != 0)
	{
		clip = walk_clip;
		clip_time *= enemy_walk_speed;
	}

	VertexOutput output = place_on_ground(
		input,
		clip,
		clip_time,
		enemy.position,
		enemy.yaw,
		-sink_depth * sink_amount * sink_amount);
	output.hit_flash = saturate(1.0 - time_since_hit / hit_flash_duration);

	// Everyone the blast would hit is highlighted, and the selected enemy the most
	const float blast_distance = distance(enemy.position, frame.blast_center);
	const bool is_in_blast = !is_dead && blast_distance < blast_radius;
	output.target_highlight = !is_dead && combat[0].selected_index == instance_index
		? 1.0
		: (is_in_blast ? 0.5 : 0.0);

	// Fully sunk, every vertex goes to the same point in front of the near plane,
	// so the rasterizer clips every triangle and nothing is drawn
	if (sink_amount >= 1.0)
	{
		output.position = float4(0.0, 0.0, -1.0, 1.0);
	}

	return output;
}

// Whirlwind turns this many times, each one the other way, and each turn takes this long in
// seconds, must match whirlwind_turns and whirlwind_turn_duration in abilities.hpp
static const uint whirlwind_turns = 6;
static const float whirlwind_turn_duration = 0.13;

// The fraction of a turn that the white impact flash lasts after each change of direction,
// while the game also freezes for a moment
static const float reversal_flash_fraction = 0.25;

// After the last turn the player leaps this high in world units for this long in seconds,
// must match whirlwind_jump_duration in abilities.hpp
static const float whirlwind_jump_height = 2.5;
static const float whirlwind_jump_duration = 0.45;
static const float whirlwind_land_time =
	float(whirlwind_turns) * whirlwind_turn_duration + whirlwind_jump_duration;

// In seconds, the white flash of the landing's impact frame, while the game freezes
static const float landing_flash_duration = 0.08;

// In seconds since Whirlwind's landing, negative before it and long after it when never cast
float time_since_landing()
{
	return frame.time - frame.last_blast_time - whirlwind_land_time;
}

struct WhirlwindSpin
{
	bool is_spinning;

	// 1 or -1, the first turn goes the same way as yaw grows
	float direction;

	// From 0 to 1, within the current turn and within the whole spin
	float turn_progress;
	float progress;

	// From 0 to 1, bright right after the spin changes direction
	float reversal_flash;
};

WhirlwindSpin whirlwind_spin()
{
	// Never cast is long ago, which is past the last turn
	const float age = frame.time - frame.last_blast_time;
	const float turn_position = age / whirlwind_turn_duration;
	const float turn = floor(turn_position);

	WhirlwindSpin spin;
	spin.is_spinning = age >= 0.0 && turn < float(whirlwind_turns);
	spin.direction = fmod(turn, 2.0) < 0.5 ? 1.0 : -1.0;
	spin.turn_progress = frac(turn_position);
	spin.progress = saturate(turn_position / float(whirlwind_turns));
	spin.reversal_flash = spin.is_spinning && turn >= 1.0
		? 1.0 - saturate(spin.turn_progress / reversal_flash_fraction)
		: 0.0;

	return spin;
}

[shader("vertex")]
VertexOutput player_vert_main(VertexInput input)
{
	// A full turn each way while Whirlwind's golden slash circles the player
	const WhirlwindSpin spin = whirlwind_spin();
	const float spin_yaw = player.yaw +
		(spin.is_spinning ? spin.direction * spin.turn_progress * 2.0 * 3.14159265 : 0.0);

	// Then up and down along a parabola, falling fastest right before the slam
	const float landing_time = time_since_landing();
	const float jump_progress = 1.0 + landing_time / whirlwind_jump_duration;
	const float jump_height = jump_progress >= 0.0 && jump_progress < 1.0
		? whirlwind_jump_height * 4.0 * jump_progress * (1.0 - jump_progress)
		: 0.0;

	VertexOutput output = place_on_ground(
		input,
		player.animation_clip,
		player.animation_time,
		player.position,
		spin_yaw,
		jump_height);

	// Weaker than an enemy's, since a surrounded player is hit several times a second,
	// but full white for the impact frames of each reversal and of the landing
	const float time_since_hit = frame.time - player.last_hit_time;
	const float landing_flash = landing_time >= 0.0
		? saturate(1.0 - landing_time / landing_flash_duration)
		: 0.0;
	output.hit_flash = max(
		0.5 * saturate(1.0 - time_since_hit / hit_flash_duration),
		max(spin.reversal_flash, landing_flash));

	return output;
}

// In world units, half the side of the ground square, which is centered below the camera
static const float ground_half_size = 200.0;

// Two counterclockwise triangles seen from above, as corners from -1 to 1 on x and z
static const float2 ground_corners[6] = {
	float2(-1.0, 1.0), float2(1.0, 1.0), float2(1.0, -1.0),
	float2(-1.0, 1.0), float2(1.0, -1.0), float2(-1.0, -1.0)
};

// In world units along the ground from the camera, where the ground fades into the clear color,
// so its edge isn't visible, beyond what the follow camera sees when zoomed out
static const float fade_start_distance = 130.0;
static const float fade_end_distance = 190.0;

// In world units, the tiles are as wide as the gap between enemies at the start
static const float tile_size = 2.0;
static const float grout_width = 0.06;

// Linear colors, and each tile's brightness varies by up to this fraction
static const float3 tile_color = float3(0.11, 0.105, 0.1);
static const float3 grout_color = float3(0.03, 0.03, 0.03);
static const float tile_variation = 0.3;

// In world units, a soft dark circle below the player, so it's clear where it stands
static const float shadow_radius = 0.9;
static const float shadow_strength = 0.6;

// The first impact spreads outward beneath the longer sword spin
static const float shock_wave_duration = 0.35;
static const float shock_wave_width = 0.3;
static const float3 shock_wave_color = float3(1.0, 0.55, 0.08);

// A curved golden blade trail, which turns white for the impact frame of each reversal
static const float slash_inner_radius = 1.0;
static const float slash_outer_radius = 4.4;
static const float slash_angular_width = 0.16;
static const float3 slash_color = float3(1.0, 0.72, 0.12);

// In world units, the front of each seismic wave, with a trail of dust behind it
static const float quake_wave_width = 0.35;
static const float3 quake_wave_color = float3(1.0, 0.45, 0.12);

// Glowing cracks out from where the sword went in, which reach this far in world units and cool
// down over this many seconds
static const uint crack_count = 9;
static const float crack_radius = 5.0;
static const float crack_width = 0.14;
static const float crack_growth_duration = 0.12;
static const float crack_duration = 1.6;
static const float3 crack_color = float3(1.0, 0.35, 0.05);

// In world units, the thrown sword's glowing blade and the streak it leaves behind
static const float thrown_blade_length = 1.8;
static const float thrown_blade_half_width = 0.14;
static const float thrown_trail_length = 3.0;
static const float3 thrown_sword_color = float3(0.45, 0.8, 1.0);

// In world units, the black hole's dark core and the swirl around it, and in seconds how long
// it takes to open and to collapse
static const float black_hole_core_radius = 0.9;
static const float black_hole_swirl_radius = 2.4;
static const float black_hole_open_duration = 0.08;
static const float black_hole_close_duration = 0.15;
static const float3 black_hole_color = float3(0.6, 0.2, 1.0);

// In seconds, how long the sword's range stays marked after the key is let go and how long it
// then takes to fade, and in world units the width of its ring
static const float strike_range_hold = 0.5;
static const float strike_range_fade = 0.4;
static const float strike_range_width = 0.12;

struct GroundOutput
{
	float4 position : SV_Position;
	float3 world_position;

	// From the combat state, which only the vertex stage can read, the same for the whole ground
	nointerpolation float2 hook_point;
	nointerpolation float hook_time;
};

// The square follows the camera while the tiles stay put, since they come from world positions
// SV_VulkanVertexID like SV_VulkanInstanceID, since SV_VertexID would need shaderDrawParameters
[shader("vertex")]
GroundOutput ground_vert_main(uint vertex_index : SV_VulkanVertexID)
{
	const float2 ground_position =
		frame.camera_position.xz + ground_corners[vertex_index] * ground_half_size;
	const float3 world_position = float3(ground_position.x, 0.0, ground_position.y);

	GroundOutput output;
	output.position = mul(frame.projection, mul(frame.view, float4(world_position, 1.0)));
	output.world_position = world_position;
	output.hook_point = combat[0].hook_point;
	output.hook_time = combat[0].hook_time;

	return output;
}

// From 0 to 1, a different value for each tile
float tile_hash(float2 tile)
{
	return frac(sin(dot(tile, float2(12.9898, 78.233))) * 43758.5453);
}

// 1 on the grout and 0 inside the tiles, with edges blurred over about a pixel
// Where a tile covers only a few pixels it turns into the average, or the lines would flicker
float grout_amount(float2 tile_coordinates, float2 pixel_size)
{
	// In tiles, how far the nearest edge is on each axis
	const float2 edge_distance = 0.5 - abs(frac(tile_coordinates) - 0.5);
	const float half_width = 0.5 * grout_width / tile_size;

	const float2 sharp = 1.0 - smoothstep(
		half_width - pixel_size, half_width + pixel_size, edge_distance);
	const float2 grout = lerp(sharp, 2.0 * half_width, saturate(4.0 * pixel_size));

	return max(grout.x, grout.y);
}

// 1 on a ring around the player as far as the sword reaches and a little inside it, shown after
// trying to throw it without a target in reach
float strike_range_amount(float2 ground_position)
{
	// Never marked is long ago, which is past the fade
	const float age = frame.time - player.strike_range_time;
	const float player_distance = distance(ground_position, player.position);
	const float pixel_size = fwidth(player_distance);
	if (age < 0.0 || age >= strike_range_hold + strike_range_fade)
	{
		return 0.0;
	}

	const float half_width = 0.5 * strike_range_width;
	const float ring = 1.0 - smoothstep(
		half_width - pixel_size, half_width + pixel_size, abs(player_distance - strike_range));
	const float inside = 0.07 * (1.0 - smoothstep(
		strike_range - pixel_size, strike_range + pixel_size, player_distance));
	const float fade = 1.0 - smoothstep(
		strike_range_hold, strike_range_hold + strike_range_fade, age);

	return max(ring, inside) * fade;
}

// 1 on the shock wave's front and less inside it, all of it fading out as it spreads
float shock_wave_amount(float2 ground_position)
{
	// Never cast is long ago, which is never this recent
	const float progress = (frame.time - frame.last_blast_time) / shock_wave_duration;
	if (progress >= 1.0)
	{
		return 0.0;
	}

	// Fast at first and slowing down, like an explosion
	const float radius = blast_radius * (1.0 - (1.0 - progress) * (1.0 - progress));

	const float distance_to_blast = distance(ground_position, frame.blast_center);
	const float pixel_size = fwidth(distance_to_blast);
	const float half_width = 0.5 * shock_wave_width;

	const float front = 1.0 - smoothstep(
		half_width - pixel_size, half_width + pixel_size, abs(distance_to_blast - radius));
	const float inside = 0.4 * (1.0 - smoothstep(
		radius - pixel_size, radius + pixel_size, distance_to_blast));

	return max(front, inside) * (1.0 - progress);
}

// The shortest signed distance between two angles, in radians
float angular_distance(float first, float second)
{
	return atan2(sin(first - second), cos(first - second));
}

// A curved sword trail with four fading copies behind it, following the spin's direction
float sword_slash_amount(float2 ground_position, WhirlwindSpin spin)
{
	if (!spin.is_spinning)
	{
		return 0.0;
	}

	const float2 from_center = ground_position - frame.blast_center;
	const float radius = length(from_center);

	// From the position, since the angle jumps by a full turn behind the center and fwidth
	// of it would draw a straight line there, and before the early return, which splits the quad
	const float angular_pixel = length(fwidth(from_center)) / max(radius, 1.0e-3);
	if (radius < slash_inner_radius || radius > slash_outer_radius)
	{
		return 0.0;
	}

	const float angle = atan2(from_center.x, from_center.y);
	const float radial_progress =
		(radius - slash_inner_radius) / (slash_outer_radius - slash_inner_radius);
	const float head =
		spin.direction * (spin.turn_progress * 2.0 * 3.14159265 - 0.75 * radial_progress);

	float trail = 0.0;
	for (uint copy = 0; copy < 5; ++copy)
	{
		const float copy_offset = spin.direction * float(copy) * 0.12;
		const float distance_to_slash = abs(angular_distance(angle, head - copy_offset));
		const float line = 1.0 - smoothstep(
			slash_angular_width - angular_pixel,
			slash_angular_width + angular_pixel,
			distance_to_slash);
		trail = max(trail, line * (1.0 - float(copy) / 5.0));
	}

	const float radial_fade = smoothstep(slash_inner_radius, slash_inner_radius + 0.5, radius) *
		(1.0 - smoothstep(slash_outer_radius - 0.7, slash_outer_radius, radius));
	const float time_fade = 1.0 - smoothstep(0.78, 1.0, spin.progress);

	return trail * radial_fade * time_fade;
}

// 1 on the front of each seismic wave and less in the dust behind it, all of them fading as they
// near the quake radius, where they stop hitting
float quake_wave_amount(float2 ground_position)
{
	const float landing_time = time_since_landing();
	const float quake_distance = distance(ground_position, frame.quake_point);
	const float pixel_size = fwidth(quake_distance);
	if (landing_time < 0.0)
	{
		return 0.0;
	}

	// Never cast is long ago, which puts every wave past the radius
	const float first_front = landing_time * quake_wave_speed;
	float amount = 0.0;
	for (uint wave = 0; wave < quake_wave_count; ++wave)
	{
		const float radius = first_front - float(wave) * quake_wave_spacing;
		if (radius <= 0.0 || radius >= quake_radius)
		{
			continue;
		}

		const float behind = radius - quake_distance;
		const float front = 1.0 - smoothstep(
			quake_wave_width - pixel_size, quake_wave_width + pixel_size, abs(behind));
		const float dust = behind > 0.0 ? 0.35 * exp(-1.5 * behind) : 0.0;
		amount = max(amount, max(front, dust) * (1.0 - radius / quake_radius));
	}

	return amount;
}

// 1 on jagged lines out from where the sword went in, which grow out at once and taper
// toward their ends, then cool down
float quake_crack_amount(float2 ground_position)
{
	const float landing_time = time_since_landing();
	const float2 from_center = ground_position - frame.quake_point;
	const float radius = length(from_center);
	const float pixel_size = length(fwidth(from_center));
	if (landing_time < 0.0 || landing_time >= crack_duration || radius >= crack_radius)
	{
		return 0.0;
	}

	// Bent by the radius so each crack wanders sideways, and every crack is a whole sector,
	// so the full turn of the angle behind the center leaves no seam
	const float angle = atan2(from_center.x, from_center.y) +
		0.25 * sin(2.3 * radius) + 0.12 * sin(6.1 * radius);
	const float sector = angle / (2.0 * 3.14159265) * float(crack_count);

	// In world units along the circle
	const float distance_to_crack =
		abs(frac(sector + 0.5) - 0.5) * 2.0 * 3.14159265 * radius / float(crack_count);
	const float half_width = 0.5 * crack_width * (1.0 - radius / crack_radius);
	const float line = 1.0 - smoothstep(
		half_width - pixel_size, half_width + pixel_size, distance_to_crack);

	const float reach = crack_radius * saturate(landing_time / crack_growth_duration);
	const float grown = 1.0 - smoothstep(reach - 0.3, reach, radius);
	const float cooling = 1.0 - smoothstep(0.3, 1.0, landing_time / crack_duration);

	return line * grown * cooling;
}

// 1 on the blade flying from the player to where it was thrown, and less on its streak
float thrown_sword_amount(float2 ground_position, float2 hook_point, float hook_time)
{
	// Never thrown is long ago, which is past the flight
	const float flight_progress = (frame.time - hook_time) / hook_flight_duration;
	const float2 path = hook_point - player.position;
	const float path_length = length(path);
	const float2 direction = path_length > 1.0e-3 ? path / path_length : float2(0.0, 1.0);
	const float2 tip = player.position + path * saturate(flight_progress);

	// Along the blade backward from its tip, and across it
	const float2 from_tip = ground_position - tip;
	const float along = -dot(from_tip, direction);
	const float across = abs(dot(from_tip, float2(-direction.y, direction.x)));
	const float pixel_size = length(fwidth(from_tip));
	if (flight_progress < 0.0 || flight_progress >= 1.0)
	{
		return 0.0;
	}

	// Pointed at the tip, then as wide as the blade up to the hilt
	const float half_width = thrown_blade_half_width * saturate(along / 0.4);
	const float width_mask =
		1.0 - smoothstep(half_width - pixel_size, half_width + pixel_size, across);
	const float length_mask = step(0.0, along) * (1.0 - smoothstep(
		thrown_blade_length - pixel_size, thrown_blade_length + pixel_size, along));
	const float blade = width_mask * length_mask;

	// Thin and fading behind the hilt, never longer than the way flown so far
	const float behind = along - thrown_blade_length;
	const float flown = path_length * saturate(flight_progress);
	const float streak = behind > 0.0 && behind < min(thrown_trail_length, flown)
		? 0.6 * (1.0 - behind / thrown_trail_length) *
			(1.0 - smoothstep(0.03 - pixel_size, 0.05 + pixel_size, across))
		: 0.0;

	return max(blade, streak);
}

// Darkens the black hole's core, adds its spinning purple swirl, and shrinks a ring from the
// pull radius toward it while it pulls the enemies in
float3 apply_black_hole(float3 color, float2 ground_position, float2 hook_point, float hook_time)
{
	const float since_impact = frame.time - hook_time - hook_flight_duration;
	const float lifetime = hook_lifetime(hook_point, player.position);
	const float2 center = hook_center(hook_point, player.position, since_impact);
	const float2 from_center = ground_position - center;
	const float radius = length(from_center);
	const float pixel_size = length(fwidth(from_center));
	if (since_impact < 0.0 || since_impact >= lifetime)
	{
		return color;
	}

	const float size = saturate(since_impact / black_hole_open_duration) *
		(1.0 - smoothstep(lifetime - black_hole_close_duration, lifetime, since_impact));
	const float core_radius = black_hole_core_radius * size;
	const float swirl_radius = black_hole_swirl_radius * size;

	// Arms that wind inward over time, and three of them, so the angle's full turn behind the
	// center leaves no seam
	const float angle = atan2(from_center.x, from_center.y);
	const float winding = 6.0 * log(max(radius, 0.05)) + 14.0 * frame.time;
	const float arms = 0.5 + 0.5 * sin(3.0 * angle + winding);
	const float swirl_mask = smoothstep(core_radius, core_radius + 0.25, radius) *
		(1.0 - smoothstep(0.5 * swirl_radius, swirl_radius, radius));
	const float core = 1.0 - smoothstep(core_radius - pixel_size, core_radius + pixel_size, radius);

	// From the pull radius down to the core, only while it holds the enemies in place
	const float hold_progress = saturate(since_impact / hook_hold_duration);
	const float pull_ring_radius = lerp(hook_radius, black_hole_core_radius, hold_progress);
	const float pull_ring = (1.0 - smoothstep(0.06 - pixel_size, 0.06 + pixel_size,
		abs(distance(ground_position, hook_point) - pull_ring_radius))) * (1.0 - hold_progress);

	color = lerp(color, float3(0.0, 0.0, 0.0), 0.95 * core);
	color += black_hole_color * (1.5 * arms * arms * swirl_mask + 1.2 * pull_ring);

	// A thin bright rim right at the edge of the core
	const float rim = 1.0 - smoothstep(0.05 - pixel_size, 0.05 + pixel_size,
		abs(radius - core_radius));

	return color + black_hole_color * 2.0 * rim * size;
}

[shader("fragment")]
float4 ground_frag_main(GroundOutput input) : SV_Target
{
	const float2 ground_position = input.world_position.xz;

	const float2 tile_coordinates = ground_position / tile_size;

	// In tiles, how much the position changes from one pixel to the next
	const float2 pixel_size = fwidth(tile_coordinates);

	// Fades out with the grout, since tiles smaller than a pixel would flicker too
	const float variation = tile_variation * (tile_hash(floor(tile_coordinates)) - 0.5) *
		(1.0 - saturate(4.0 * max(pixel_size.x, pixel_size.y)));
	const float3 albedo = lerp(
		tile_color * (1.0 + variation),
		grout_color,
		grout_amount(tile_coordinates, pixel_size));

	// Lambert with the ground's normal, which points straight up, and no highlight, since
	// a shiny floor would distract from the characters
	const float diffuse = max(normalize(frame.light_direction).y, 0.0);
	float3 color = albedo * (frame.light_color * diffuse + frame.ambient_strength);

	// Set before the player's draw, and the same layout keeps them for this one
	const float player_distance = distance(ground_position, player.position);
	const float shadow = 1.0 - smoothstep(0.3 * shadow_radius, shadow_radius, player_distance);
	color *= 1.0 - shadow_strength * shadow;

	// Unlit, so it's equally visible in light and in shadow
	color = lerp(color, shock_wave_color, shock_wave_amount(ground_position));

	const WhirlwindSpin spin = whirlwind_spin();
	const float3 blade_color = lerp(slash_color * 1.4, float3(3.0, 3.0, 3.0), spin.reversal_flash);
	color += blade_color * sword_slash_amount(ground_position, spin);

	color = lerp(color, quake_wave_color, quake_wave_amount(ground_position));
	color = lerp(color, crack_color * 2.0, quake_crack_amount(ground_position));

	color = lerp(color, thrown_sword_color, strike_range_amount(ground_position));

	color = apply_black_hole(color, ground_position, input.hook_point, input.hook_time);
	color += thrown_sword_color * 2.0 *
		thrown_sword_amount(ground_position, input.hook_point, input.hook_time);

	const float camera_distance = distance(ground_position, frame.camera_position.xz);
	color *= 1.0 - smoothstep(fade_start_distance, fade_end_distance, camera_distance);

	return float4(color, 1.0);
}

// Texture coordinates arrive interpolated, so each fragment samples its own point
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	const float4 texture_color = texture.Sample(input.texture_coordinates);

	// Interpolation shortens the normals, so they're normalized again
	const float3 normal = normalize(input.normal);

	const float3 to_light = normalize(frame.light_direction);
	const float3 to_camera = normalize(frame.camera_position - input.world_position);

	// Lambert, brightest facing the light and none once the surface turns away from it
	const float diffuse = max(dot(normal, to_light), 0.0);

	// Blinn-Phong, brightest when the normal points halfway between the light and the camera
	// Only on lit surfaces, or highlights would show through from behind
	const float3 halfway = normalize(to_light + to_camera);
	const float specular = diffuse > 0.0
		? pow(max(dot(normal, halfway), 0.0), frame.shininess) * frame.specular_strength
		: 0.0;

	// The texture is sRGB, so sampling returns linear colors, where light adds up correctly
	// The highlight is the color of the light, so it's added after tinting by the texture
	const float3 lighting = frame.light_color * diffuse + frame.ambient_strength;
	float3 color = texture_color.rgb * lighting + frame.light_color * specular;

	// The target glows at its silhouette, where the surface turns away from the camera
	const float rim = 1.0 - saturate(dot(normal, to_camera));
	color += aim_color * input.target_highlight * (0.15 + 1.5 * rim * rim);

	// Unlit, so a hit reads the same in light and in shadow
	color = lerp(color, float3(1.0, 1.0, 1.0), 0.8 * input.hit_flash);

	return float4(color, texture_color.a);
}
