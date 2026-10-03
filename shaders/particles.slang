// Must match CameraUniforms in renderer.cpp, and binding 0 of set 0 in the descriptor set layout of the graphics pipeline
struct CameraUniforms
{
	float4x4 view;
	float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniforms> camera;

// Must match Particle in particle_system.cpp, all float4 so both sides agree on the layout, with w unused in the first two
struct Particle
{
	float4 position;
	float4 velocity;
	float4 color;
};

// Must match the update descriptor set layout in particle_system.cpp
// Bindings 1 and 2, since binding 0 of this file is the camera used when drawing
[[vk::binding(1, 0)]]
StructuredBuffer<Particle> previous_particles;

[[vk::binding(2, 0)]]
RWStructuredBuffer<Particle> current_particles;

// Must match UpdateConstants in particle_system.cpp, pushed before each dispatch
struct UpdateConstants
{
	float4 box_min;
	float4 box_max;
	float delta_time;
	uint particle_count;
};

[[vk::push_constant]]
ConstantBuffer<UpdateConstants> update_constants;

// Moves each particle from where it was in the previous frame, bouncing off the walls of the box
// 256 invocations per work group, which must match work_group_size in particle_system.cpp
[shader("compute")]
[numthreads(256, 1, 1)]
void update_main(uint3 thread_id : SV_DispatchThreadID)
{
	const uint index = thread_id.x;

	// The last work group can go past the end when the count isn't a multiple of its size
	if (index >= update_constants.particle_count)
	{
		return;
	}

	Particle particle = previous_particles[index];
	particle.position.xyz += particle.velocity.xyz * update_constants.delta_time;

	// Past a wall, the velocity points back inside, so a particle can't get stuck bouncing on the outside
	const bool3 is_below = particle.position.xyz < update_constants.box_min.xyz;
	const bool3 is_above = particle.position.xyz > update_constants.box_max.xyz;
	particle.velocity.xyz = select(is_below, abs(particle.velocity.xyz), select(is_above, -abs(particle.velocity.xyz), particle.velocity.xyz));
	particle.position.xyz = clamp(particle.position.xyz, update_constants.box_min.xyz, update_constants.box_max.xyz);

	current_particles[index] = particle;
}

// Read from the storage buffer as vertices, locations follow declaration order and must match particle_system.cpp
struct ParticleInput
{
	float3 position;
	float4 color;
};

// Passed to the fragment shader
struct ParticleOutput
{
	float4 position : SV_Position;
	float point_size : SV_PointSize;
	float4 color;
};

[shader("vertex")]
ParticleOutput particle_vert_main(ParticleInput input)
{
	ParticleOutput output;
	output.position = mul(camera.projection, mul(camera.view, float4(input.position, 1.0)));
	// In pixels, smaller when farther away like the rest of the scene, but never under one pixel
	output.point_size = max(1.0, 64.0 / output.position.w);
	output.color = input.color;

	return output;
}

// The color from the vertex shader, and where the fragment is inside its point
struct ParticleFragmentInput
{
	float4 color;
	// From (0, 0) at the top left of the point to (1, 1) at the bottom right
	float2 point_coordinates : SV_PointCoord;
};

// Round points that fade from the center to the edge, through an alpha the blending uses
[shader("fragment")]
float4 particle_frag_main(ParticleFragmentInput input) : SV_Target
{
	// 1 at the center, down to 0 at the edge of the circle that fits in the point
	const float fade = saturate(1.0 - 2.0 * length(input.point_coordinates - 0.5));

	return float4(input.color.rgb, input.color.a * fade);
}
