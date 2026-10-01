// Hardcoded triangle until vertex buffers are added
static const float2 positions[3] = float2[](
	float2(0.0, -0.5),
	float2(0.5, 0.5),
	float2(-0.5, 0.5)
);

static const float3 colors[3] = float3[](
	float3(1.0, 0.0, 0.0),
	float3(0.0, 1.0, 0.0),
	float3(0.0, 0.0, 1.0)
);

// Data passed from the vertex shader to the fragment shader
struct VertexOutput
{
	float4 position : SV_Position;
	float3 color;
};

[shader("vertex")]
VertexOutput vert_main(uint vertex_id : SV_VertexID)
{
	VertexOutput output;
	output.position = float4(positions[vertex_id], 0.0, 1.0);
	output.color = colors[vertex_id];

	return output;
}

// The color is interpolated between the three vertices
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	return float4(input.color, 1.0);
}
