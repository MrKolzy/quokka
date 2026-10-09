// Must match CameraUniforms in renderer.cpp
struct CameraUniforms
{
	float4x4 view;
	float4x4 projection;
};

// Must match camera_binding in graphics_pipeline.cpp
[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniforms> camera;

// Must match ObjectConstants in graphics_pipeline.hpp, pushed before drawing each object
struct ObjectConstants
{
	float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<ObjectConstants> object_constants;

// Image and sampler in one descriptor, must match texture_binding in graphics_pipeline.cpp
[[vk::binding(1, 0)]]
Sampler2D texture;

// A fixed sun until lights become part of the scene
// In world space, pointing toward the light, and normalized where it's used
static const float3 light_direction = float3(-0.5, 1.0, 0.4);
static const float3 light_color = float3(1.0, 1.0, 1.0);

// Reaches every surface equally, so the ones facing away from the light aren't black
static const float3 ambient_color = float3(0.15, 0.15, 0.15);

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
	float3 normal;
	float2 texture_coordinates;
};

[shader("vertex")]
VertexOutput vert_main(VertexInput input)
{
	const float4 world_position = mul(object_constants.model, float4(input.position, 1.0));
	const float4 view_position = mul(camera.view, world_position);

	VertexOutput output;
	output.position = mul(camera.projection, view_position);

	// In world space, w is 0 so the translation is skipped, enough while the scale is uniform
	output.normal = mul(object_constants.model, float4(input.normal, 0.0)).xyz;

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

	// Lambert, brightest facing the light and none once the surface turns away from it
	const float diffuse = max(dot(normal, normalize(light_direction)), 0.0);

	// The texture is sRGB, so sampling returns linear colors, where light adds up correctly
	const float3 lighting = light_color * diffuse + ambient_color;

	return float4(texture_color.rgb * lighting, texture_color.a);
}
