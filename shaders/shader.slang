// Read from the vertex buffer, locations follow declaration order and must match Vertex::attribute_descriptions
struct VertexInput
{
	float2 position;
	float3 color;
};

// Passed to the fragment shader
struct VertexOutput
{
	float4 position : SV_Position;
	float3 color;
};

[shader("vertex")]
VertexOutput vert_main(VertexInput input)
{
	VertexOutput output;
	output.position = float4(input.position, 0.0, 1.0);
	output.color = input.color;

	return output;
}

// The color is interpolated between the three vertices
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	return float4(input.color, 1.0);
}
