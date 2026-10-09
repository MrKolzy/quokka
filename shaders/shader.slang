// Must match FrameUniforms in renderer.cpp
// Each float3 is followed by a float, which fills its 16 bytes the same way as in C++
struct FrameUniforms
{
	float4x4 view;
	float4x4 projection;

	float3 camera_position;
	float ambient_strength;

	// In world space, pointing toward the light, and normalized where it's used
	float3 light_direction;
	float specular_strength;

	float3 light_color;
	float shininess;
};

// Must match frame_binding in graphics_pipeline.cpp
[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> frame;

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

[shader("vertex")]
VertexOutput vert_main(VertexInput input)
{
	const float4 world_position = mul(object_constants.model, float4(input.position, 1.0));
	const float4 view_position = mul(frame.view, world_position);

	VertexOutput output;
	output.position = mul(frame.projection, view_position);
	output.world_position = world_position.xyz;

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
