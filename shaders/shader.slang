// Must match CameraUniforms in renderer.cpp, and binding 0 of set 0 in the descriptor set layout
struct CameraUniforms
{
	float4x4 view;
	float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniforms> camera;

// Must match ObjectConstants in graphics_pipeline.hpp, pushed before drawing each object
struct ObjectConstants
{
	float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<ObjectConstants> object_constants;

// Image and sampler in a single descriptor, must match binding 1 of set 0 in the descriptor set layout
[[vk::binding(1, 0)]]
Sampler2D texture;

// Read from the vertex buffer, locations follow declaration order and must match Vertex::attribute_descriptions
struct VertexInput
{
	float3 position;
	float3 color;
	float2 texture_coordinates;
};

// Passed to the fragment shader
struct VertexOutput
{
	float4 position : SV_Position;
	float3 color;
	float2 texture_coordinates;
};

[shader("vertex")]
VertexOutput vert_main(VertexInput input)
{
	VertexOutput output;
	// Model to world, world to view and view to clip space, applied from right to left
	output.position = mul(camera.projection, mul(camera.view, mul(object_constants.model, float4(input.position, 1.0))));
	output.color = input.color;
	output.texture_coordinates = input.texture_coordinates;

	return output;
}

// Texture coordinates are interpolated between the vertices, so each fragment samples its own point of the texture
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	return texture.Sample(input.texture_coordinates);
}
