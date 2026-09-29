static float2 positions[3] = float2[](
	float2(0.0, -0.5),
	float2(0.5, 0.5),
	float2(-0.5, 0.5)
);

static float3 colors[3] = float3[](
	float3(1.0, 0.0, 0.0),
	float3(0.0, 1.0, 0.0),
	float3(0.0, 0.0, 1.0)
);

// Data passed from the vertex shader to the fragment shader
struct VertexOutput
{
	float3 color;
	float4 sv_position : SV_Position;
};

// Runs once per vertex, the triangle positions and colors are hardcoded above
[shader("vertex")]
VertexOutput vert_main(uint vertex_id : SV_VertexID)
{
	VertexOutput output;
	output.sv_position = float4(positions[vertex_id], 0.0, 1.0);
	output.color = colors[vertex_id];
	return output;
}

// Runs once per fragment, the color is interpolated between the three vertices
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	return float4(input.color, 1.0);
}
