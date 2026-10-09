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

// Linear, the color that marks what the player aims at, on the ground and around the target
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
	}

	VertexOutput output = place_on_ground(
		input,
		clip,
		clip_time,
		enemy.position,
		enemy.yaw,
		-sink_depth * sink_amount * sink_amount);
	output.hit_flash = saturate(1.0 - time_since_hit / hit_flash_duration);

	// Everyone the blast would hit is selected, and the strike's target the most
	const bool is_in_blast = !is_dead && distance(enemy.position, frame.aim_point) < blast_radius;
	output.target_highlight = is_target(combat[0].target_key, instance_index)
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

[shader("vertex")]
VertexOutput player_vert_main(VertexInput input)
{
	VertexOutput output = place_on_ground(
		input,
		player.animation_clip,
		player.animation_time,
		player.position,
		player.yaw,
		0.0);

	// Weaker than an enemy's, since a surrounded player is hit several times a second
	const float time_since_hit = frame.time - player.last_hit_time;
	output.hit_flash = 0.5 * saturate(1.0 - time_since_hit / hit_flash_duration);

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

// In world units, the ring around the aim point is as wide as the blast, so it shows who it hits,
// and it glows so it reads on any tile
static const float aim_ring_width = 0.08;
static const float aim_fill_opacity = 0.08;

// The blast's shock wave spreads to blast_radius over its duration in seconds, fading out,
// with a hot linear color, brighter than the aim color
static const float shock_wave_duration = 0.35;
static const float shock_wave_width = 0.3;
static const float3 shock_wave_color = float3(1.0, 0.35, 0.08);

struct GroundOutput
{
	float4 position : SV_Position;
	float3 world_position;
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

// 1 on the ring around the aim point and 0 away from it, with a faint fill inside
float aim_marker_amount(float2 ground_position)
{
	const float distance_to_aim = distance(ground_position, frame.aim_point);
	const float pixel_size = fwidth(distance_to_aim);
	const float half_width = 0.5 * aim_ring_width;

	const float ring = 1.0 - smoothstep(
		half_width - pixel_size, half_width + pixel_size, abs(distance_to_aim - blast_radius));
	const float fill = aim_fill_opacity * (1.0 - smoothstep(
		blast_radius - pixel_size, blast_radius + pixel_size, distance_to_aim));

	return max(ring, fill);
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

	const float distance_to_blast = distance(ground_position, frame.last_blast_point);
	const float pixel_size = fwidth(distance_to_blast);
	const float half_width = 0.5 * shock_wave_width;

	const float front = 1.0 - smoothstep(
		half_width - pixel_size, half_width + pixel_size, abs(distance_to_blast - radius));
	const float inside = 0.4 * (1.0 - smoothstep(
		radius - pixel_size, radius + pixel_size, distance_to_blast));

	return max(front, inside) * (1.0 - progress);
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
	color = lerp(color, aim_color, aim_marker_amount(ground_position));
	color = lerp(color, shock_wave_color, shock_wave_amount(ground_position));

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
