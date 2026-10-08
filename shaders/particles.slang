// Must match CameraUniforms in renderer.cpp
struct CameraUniforms
{
	float4x4 view;
	float4x4 projection;
};

// Must match camera_binding in graphics_pipeline.cpp
[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniforms> camera;

// Must match Particle in particle_system.cpp
struct Particle
{
	float4 position;
	float4 velocity;
	float4 color;
};

// Must match previous_particles_binding in particle_system.cpp
[[vk::binding(1, 0)]]
StructuredBuffer<Particle> previous_particles;

// Must match current_particles_binding in particle_system.cpp
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

// Must match work_group_size in particle_system.cpp
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

	const float3 box_min = update_constants.box_min.xyz;
	const float3 box_max = update_constants.box_max.xyz;

	Particle particle = previous_particles[index];
	particle.position.xyz += particle.velocity.xyz * update_constants.delta_time;

	// Past a wall, the velocity points back inside, so a particle can't get stuck outside
	const bool3 is_below = particle.position.xyz < box_min;
	const bool3 is_above = particle.position.xyz > box_max;
	const float3 abs_velocity = abs(particle.velocity.xyz);
	particle.velocity.xyz = select(is_below, abs_velocity, particle.velocity.xyz);
	particle.velocity.xyz = select(is_above, -abs_velocity, particle.velocity.xyz);
	particle.position.xyz = clamp(particle.position.xyz, box_min, box_max);

	current_particles[index] = particle;
}

// Locations follow declaration order and must match attribute_descriptions in particle_system.cpp
struct ParticleInput
{
	float3 position;
	float4 color;
};

struct ParticleOutput
{
	float4 position : SV_Position;
	float point_size : SV_PointSize;
	float4 color;
};

[shader("vertex")]
ParticleOutput particle_vert_main(ParticleInput input)
{
	const float4 view_position = mul(camera.view, float4(input.position, 1.0));

	ParticleOutput output;
	output.position = mul(camera.projection, view_position);
	output.color = input.color;

	// In pixels, smaller when farther away like the rest of the scene, but never under one pixel
	output.point_size = max(1.0, 64.0 / output.position.w);

	return output;
}

// Separate from ParticleOutput because SV_PointCoord only exists in the fragment stage
struct ParticleFragmentInput
{
	float4 color;

	// From (0, 0) at the top left of the point to (1, 1) at the bottom right
	float2 point_coordinates : SV_PointCoord;
};

// Fades through the alpha, because the blending weighs each color by it
[shader("fragment")]
float4 particle_frag_main(ParticleFragmentInput input) : SV_Target
{
	// 1 at the center, down to 0 at the edge of the circle that fits in the point
	const float fade = saturate(1.0 - 2.0 * length(input.point_coordinates - 0.5));

	return float4(input.color.rgb, input.color.a * fade);
}
