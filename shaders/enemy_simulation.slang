#include "enemy.slang"

// Written in place, one enemy per thread, must match state_binding in enemy_simulation.cpp
[[vk::binding(0, 0)]]
RWStructuredBuffer<EnemyState> enemies;

// A copy from before this update, so every thread reads its neighbors' old positions
// while the others write their new ones, must match previous_binding in enemy_simulation.cpp
[[vk::binding(1, 0)]]
StructuredBuffer<EnemyState> previous_enemies;

// Must match SimulationConstants in enemy_simulation.cpp
struct SimulationConstants
{
	// On the ground, as world x and z
	float2 target;

	// Capped on the CPU, so a long frame doesn't make the enemies jump
	float delta_seconds;

	uint enemy_count;
};

[[vk::push_constant]]
ConstantBuffer<SimulationConstants> simulation;

// In world units, and the grid starts with the enemies 2 units apart
static const float aggro_radius = 8.0;
static const float stop_distance = 2.0;
static const float separation_radius = 1.6;

// In world units per second
static const float chase_speed = 3.0;
static const float separation_speed = 4.0;
static const float max_speed = 6.0;

// Must match workgroup_size in enemy_simulation.cpp
static const uint workgroup_size = 64;

// Each workgroup loads its neighbors' positions here one tile at a time, so every position
// is read from memory once per workgroup instead of once per thread
groupshared float2 tile_positions[workgroup_size];

[shader("compute")]
[numthreads(workgroup_size, 1, 1)]
void update_main(uint3 thread_id : SV_DispatchThreadID, uint3 local_id : SV_GroupThreadID)
{
	const uint index = thread_id.x;

	// The last workgroup can have threads past the end, which still help load the tiles
	const bool is_enemy = index < simulation.enemy_count;
	EnemyState enemy = previous_enemies[min(index, simulation.enemy_count - 1)];

	// Pushed away from every neighbor that's too close, harder the closer it is
	// Every enemy is checked against every other, fine for thousands but not for far more
	float2 push = float2(0.0, 0.0);
	for (uint tile_start = 0; tile_start < simulation.enemy_count; tile_start += workgroup_size)
	{
		const uint other_index = tile_start + local_id.x;

		// Far away when past the end, so it never pushes anyone
		tile_positions[local_id.x] = other_index < simulation.enemy_count
			? previous_enemies[other_index].position
			: float2(1.0e9, 1.0e9);

		GroupMemoryBarrierWithGroupSync();

		for (uint tile_index = 0; tile_index < workgroup_size; ++tile_index)
		{
			const float2 away = enemy.position - tile_positions[tile_index];
			const float distance_squared = dot(away, away);

			// Itself is at distance 0, and so is anyone exactly on top, who has no direction
			if (distance_squared > 1.0e-6 && distance_squared < separation_radius * separation_radius)
			{
				const float distance = sqrt(distance_squared);
				push += away / distance * (1.0 - distance / separation_radius);
			}
		}

		// Everyone must be done with this tile before the next one overwrites it
		GroupMemoryBarrierWithGroupSync();
	}

	// Only after the last barrier, which every thread of the workgroup has to reach
	if (!is_enemy)
	{
		return;
	}

	const float2 to_target = simulation.target - enemy.position;
	const float target_distance = length(to_target);

	if (target_distance < aggro_radius)
	{
		enemy.is_chasing = 1;
	}

	float2 velocity = push * separation_speed;

	if (enemy.is_chasing != 0 && target_distance > stop_distance)
	{
		velocity += to_target / target_distance * chase_speed;
	}

	// Facing the target while chasing it, the same convention as rotate_around_up
	if (enemy.is_chasing != 0 && target_distance > 1.0e-3)
	{
		enemy.yaw = atan2(to_target.x, to_target.y);
	}

	// A crowd pushing from every side could otherwise launch someone
	const float speed = length(velocity);
	if (speed > max_speed)
	{
		velocity *= max_speed / speed;
	}

	enemy.position += velocity * simulation.delta_seconds;
	enemies[index] = enemy;
}
