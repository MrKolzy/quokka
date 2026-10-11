// Included by every shader that reads the frame uniforms, so they share one definition

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

	// In pixels, for what keeps its size on screen whatever the distance, like damage numbers
	float2 viewport_size;

	// On the ground as world x and z, the player's position, where Whirlwind's effects are centered
	float2 blast_center;

	// In seconds since the start, for animations computed on the GPU
	float time;

	// In seconds since the start, when Whirlwind was last cast
	float last_blast_time;

	// On the ground as world x and z, where Whirlwind landed and its quake spreads from
	float2 quake_point;
};

// Must match frame_binding in graphics_pipeline.cpp
[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> frame;
