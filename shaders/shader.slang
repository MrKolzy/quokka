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

// Must match PlayerConstants in graphics_pipeline.hpp
struct PlayerConstants
{
	// On the ground, as world x and z
	float2 position;

	// The same convention as EnemyState
	float yaw;
};

[[vk::push_constant]]
ConstantBuffer<PlayerConstants> player;

// Locations follow declaration order and must match Vertex::attribute_descriptions
struct VertexInput
{
	float3 position;
	float3 normal;
	float2 texture_coordinates;
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

// A dead enemy stays up for the delay, then sinks this deep in world units, deeper than it's
// tall, so the ground drawn after it hides it
static const float death_delay = 0.2;
static const float sink_duration = 1.2;
static const float sink_depth = 3.0;

// The same rotation as glm::rotate around y, so a quarter turn takes +z to +x
float3 rotate_around_up(float3 value, float angle)
{
	const float cosine = cos(angle);
	const float sine = sin(angle);

	return float3(value.x * cosine + value.z * sine, value.y, value.z * cosine - value.x * sine);
}

// Turned to face along the yaw, then moved to its spot on the ground, or below it when sinking
// glTF models face +z, which is yaw 0, so they need no extra turn
VertexOutput place_on_ground(VertexInput input, float2 ground_position, float yaw, float height)
{
	const float3 world_position = rotate_around_up(input.position, yaw) +
		float3(ground_position.x, height, ground_position.y);

	const float4 view_position = mul(frame.view, float4(world_position, 1.0));

	VertexOutput output;
	output.position = mul(frame.projection, view_position);
	output.world_position = world_position;

	// Only rotated, so the normal stays unit length and needs no inverse transpose
	output.normal = rotate_around_up(input.normal, yaw);

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

	// Never hit is minus infinity, which saturates to no flash and no sinking
	const float time_since_hit = frame.time - enemy.last_hit_time;
	const float sink_amount = is_dead
		? saturate((time_since_hit - death_delay) / sink_duration)
		: 0.0;

	VertexOutput output = place_on_ground(
		input, enemy.position, enemy.yaw, -sink_depth * sink_amount * sink_amount);
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
	return place_on_ground(input, player.position, player.yaw, 0.0);
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
	// Never cast is minus infinity, which is never this recent
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
