#include "enemy.slang"
#include "frame.slang"

// Written by the simulation every frame, must match enemy_binding in graphics_pipeline.cpp
[[vk::binding(2, 0)]]
StructuredBuffer<EnemyState> enemies;

// In radians, turns the viking room so its open side faces along the enemy's yaw
static const float model_yaw = -1.5707964;

// Image and sampler in one descriptor, must match texture_binding in graphics_pipeline.cpp
[[vk::binding(1, 0)]]
Sampler2D texture;

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
};

// The same rotation as glm::rotate around y, so a quarter turn takes +z to +x
float3 rotate_around_up(float3 value, float angle)
{
	const float cosine = cos(angle);
	const float sine = sin(angle);

	return float3(value.x * cosine + value.z * sine, value.y, value.z * cosine - value.x * sine);
}

// SV_VulkanInstanceID includes the first instance of the draw, unlike SV_InstanceID,
// which matters because each recording thread draws its own range of instances
[shader("vertex")]
VertexOutput vert_main(VertexInput input, uint instance_index : SV_VulkanInstanceID)
{
	const EnemyState enemy = enemies[instance_index];

	// Turned to face where the enemy looks, then moved to its spot on the ground
	const float angle = enemy.yaw + model_yaw;
	const float3 world_position = rotate_around_up(input.position, angle) +
		float3(enemy.position.x, 0.0, enemy.position.y);

	const float4 view_position = mul(frame.view, float4(world_position, 1.0));

	VertexOutput output;
	output.position = mul(frame.projection, view_position);
	output.world_position = world_position;

	// Only rotated, so the normal stays unit length and needs no inverse transpose
	output.normal = rotate_around_up(input.normal, angle);

	output.texture_coordinates = input.texture_coordinates;

	return output;
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
	const float3 lit_color = texture_color.rgb * lighting + frame.light_color * specular;

	return float4(lit_color, texture_color.a);
}
