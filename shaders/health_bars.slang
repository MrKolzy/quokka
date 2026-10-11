#include "enemy.slang"
#include "frame.slang"

// Read for each enemy's health and position, must match enemy_binding in graphics_pipeline.cpp
[[vk::binding(1, 0)]]
StructuredBuffer<EnemyState> enemies;

// Just above the skeleton's head, which is about 2.2 units tall, and below where the damage
// numbers start, so they rise away from it
static const float anchor_height = 2.3;

// In screen pixels with the border, the same at any distance from the camera
static const float2 bar_size = float2(36.0, 5.0);
static const float border_width = 1.0;

// Linear colors, red like the enemies' bars in most games, see through like the debug panels
static const float3 fill_color = float3(0.75, 0.03, 0.02);
static const float3 empty_color = float3(0.02, 0.02, 0.02);
static const float3 border_color = float3(0.0, 0.0, 0.0);
static const float opacity = 0.85;

// The two triangles of a quad, from 0 to 1 with y up
static const float2 quad_corners[6] = {
	float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
	float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

struct VertexOutput
{
	float4 position : SV_Position;

	// In screen pixels from the bar's bottom left corner
	float2 bar_position;

	// The same for the whole bar, so it isn't interpolated
	nointerpolation float health_fraction;
};

// Outside the clip volume, so both triangles are dropped
VertexOutput hidden_vertex()
{
	VertexOutput output;
	output.position = float4(2.0, 2.0, 2.0, 1.0);
	output.bar_position = float2(0.0, 0.0);
	output.health_fraction = 0.0;

	return output;
}

// Each instance is one enemy's bar
// Only the wounded ones and the ones inside the blast circle have it, so a full crowd isn't
// covered in bars
[shader("vertex")]
VertexOutput vert_main(
	uint vertex_index : SV_VulkanVertexID,
	uint instance_index : SV_VulkanInstanceID)
{
	const EnemyState enemy = enemies[instance_index];
	const float health_fraction = saturate(enemy.health / enemy_max_health);
	const bool is_wounded = health_fraction < 1.0;
	const bool is_in_blast = distance(enemy.position, frame.blast_center) < blast_radius;

	if (enemy.health <= 0.0 || (!is_wounded && !is_in_blast))
	{
		return hidden_vertex();
	}

	// Follows the enemy while it moves, the way nameplates do
	const float3 position = float3(enemy.position.x, anchor_height, enemy.position.y);
	const float4 anchor = mul(frame.projection, mul(frame.view, float4(position, 1.0)));

	// Behind the camera
	if (anchor.w <= 0.0)
	{
		return hidden_vertex();
	}

	// Centered on its anchor, and the flipped viewport makes clip space y point up like here
	const float2 corner = quad_corners[vertex_index];
	const float2 offset = (corner - 0.5) * bar_size;

	VertexOutput output;
	output.position = float4(anchor.xy / anchor.w + offset * 2.0 / frame.viewport_size, 0.0, 1.0);
	output.bar_position = corner * bar_size;
	output.health_fraction = health_fraction;

	return output;
}

// The border around the bar, then the health filled from the left
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	const float2 inner_position = input.bar_position - border_width;
	const float2 inner_size = bar_size - 2.0 * border_width;

	if (any(inner_position < 0.0) || any(inner_position > inner_size))
	{
		return float4(border_color, opacity);
	}

	const bool is_filled = inner_position.x / inner_size.x < input.health_fraction;

	return float4(is_filled ? fill_color : empty_color, opacity);
}
