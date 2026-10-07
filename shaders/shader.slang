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

// Locations follow declaration order and must match Vertex::attribute_descriptions
struct VertexInput
{
	float3 position;
	float3 color;
	float2 texture_coordinates;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float3 color;
	float2 texture_coordinates;
};

[shader("vertex")]
VertexOutput vert_main(VertexInput input)
{
	const float4 world_position = mul(object_constants.model, float4(input.position, 1.0));
	const float4 view_position = mul(camera.view, world_position);

	VertexOutput output;
	output.position = mul(camera.projection, view_position);
	output.color = input.color;
	output.texture_coordinates = input.texture_coordinates;

	return output;
}

// Texture coordinates arrive interpolated, so each fragment samples its own point
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	return texture.Sample(input.texture_coordinates);
}
