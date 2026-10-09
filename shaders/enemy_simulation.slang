#include "enemy.slang"

// Written in place, one enemy per thread, must match state_binding in enemy_simulation.cpp
[[vk::binding(0, 0)]]
RWStructuredBuffer<EnemyState> enemies;

// A copy from before this update, so every thread reads its neighbors' old positions
// while the others write their new ones, must match previous_binding in enemy_simulation.cpp
[[vk::binding(1, 0)]]
StructuredBuffer<EnemyState> previous_enemies;

// Partly cleared before every update, must match combat_binding in enemy_simulation.cpp
[[vk::binding(2, 0)]]
RWStructuredBuffer<CombatState> combat;

// The ring every hit spawns its number into, must match damage_number_binding
// in enemy_simulation.cpp
[[vk::binding(3, 0)]]
RWStructuredBuffer<DamageNumber> damage_numbers;

// Must match the cast flags in enemy_simulation.cpp
// The test gives every enemy a number without hurting it, so benchmarks can repeat it
static const uint blast_cast = 1;
static const uint test_cast = 2;

// Must match SimulationConstants in enemy_simulation.cpp
struct SimulationConstants
{
	// On the ground, as world x and z
	float2 target;

	// Capped on the CPU, so a long frame doesn't make the enemies jump
	float delta_seconds;

	uint enemy_count;

	// On the ground, as world x and z, where the blast lands and the target is picked
	float2 aim_point;

	// In seconds since the start, when the hits happen
	float time;

	// Abilities cast this frame, as cast flags
	uint casts;

	// Minimum and maximum, from the ability definitions in abilities.hpp
	float2 strike_damage;
	float2 blast_damage;

	// In percent, and critical hits deal double damage
	uint critical_chance;

	// 0 or 1, the enemies stop attacking a dead target
	uint is_target_alive;
};

[[vk::push_constant]]
ConstantBuffer<SimulationConstants> simulation;

// In world units, and the grid starts with the enemies 2 units apart
static const float aggro_radius = 8.0;
static const float stop_distance = 2.0;
static const float separation_radius = 1.6;

// In world units around the aim point, generous because the cursor points at the ground
// behind an enemy's body, about 1 unit away with the follow camera's angle
static const float pick_radius = 1.5;

// In world units per second
static const float chase_speed = 3.0;
static const float separation_speed = 4.0;
static const float max_speed = 6.0;

// In world units, a bit past stop_distance, so the enemies that stopped next to the target
// reach it, and only the first ring around it does, since the separation keeps the rest out
static const float attack_range = 2.4;

// Damage per attack, and seconds between attacks, each one a little longer or shorter so a crowd
// doesn't hit in step, balanced around the player's health in player.cpp
static const uint attack_damage = 6;
static const float attack_interval = 1.5;
static const float attack_interval_variation = 0.3;

// The test isn't an ability, so its damage only lives here
static const float2 test_damage = float2(100.0, 999.0);

// So an enemy hit by both abilities in the same frame rolls its damage twice
static const uint blast_seed = 1;
static const uint strike_seed = 2;
static const uint test_seed = 3;

// Must match workgroup_size in enemy_simulation.cpp
static const uint workgroup_size = 64;

// Each workgroup loads its neighbors' positions here one tile at a time, so every position
// is read from memory once per workgroup instead of once per thread
groupshared float2 tile_positions[workgroup_size];

// PCG, a well mixed pseudo-random number from any number
uint pcg_hash(uint value)
{
	const uint state = value * 747796405u + 2891336453u;
	const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;

	return (word >> 22u) ^ word;
}

// A hit's number above the enemy, and its damage toward the frame's total
// Once the ring is full, the oldest numbers are overwritten
void spawn_number(uint index, uint damage, bool is_critical)
{
	uint count;
	InterlockedAdd(combat[0].spawned_number_count, 1, count);

	damage_numbers[count % damage_number_capacity] = {
		index, simulation.time, damage, is_critical ? 1u : 0u
	};

	InterlockedAdd(combat[0].frame_damage, damage);
}

// Different for every enemy, frame and ability, with its number spawned
// Returns the damage, doubled by a critical hit
uint roll_hit(uint index, uint seed, float2 damage_range)
{
	const uint random = pcg_hash(index ^ pcg_hash(asuint(simulation.time) ^ seed));

	// The low 16 bits pick the damage and the high ones the critical hit
	const float roll = float(random & 0xFFFF) / 65535.0;
	const uint damage = uint(lerp(damage_range.x, damage_range.y, roll));
	const bool is_critical = (random >> 16) % 100 < simulation.critical_chance;
	const uint dealt_damage = is_critical ? 2 * damage : damage;

	spawn_number(index, dealt_damage, is_critical);

	return dealt_damage;
}

// The hit pulls the enemy however far it is
void take_hit(inout EnemyState enemy, uint index, uint seed, float2 damage_range)
{
	enemy.health -= float(roll_hit(index, seed, damage_range));
	enemy.last_hit_time = simulation.time;
	enemy.is_chasing = 1;
}

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
		const EnemyState other = previous_enemies[min(other_index, simulation.enemy_count - 1)];

		// Far away when past the end or dead, so it never pushes anyone
		tile_positions[local_id.x] = other_index < simulation.enemy_count && other.health > 0.0
			? other.position
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

	// The dead too, so a benchmark always shows a number for every enemy
	if ((simulation.casts & test_cast) != 0)
	{
		roll_hit(index, test_seed, test_damage);
	}

	// Still written, because after a reset the states must be overwritten by the copy
	if (enemy.health <= 0.0)
	{
		enemies[index] = enemy;

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

	const bool can_attack = simulation.is_target_alive != 0 && enemy.is_chasing != 0 &&
		distance(enemy.position, simulation.target) < attack_range;
	if (can_attack && simulation.time >= enemy.next_attack_time)
	{
		InterlockedAdd(combat[0].player_damage, attack_damage);

		const float random = float(pcg_hash(index ^ asuint(simulation.time)) & 0xFFFF) / 65535.0;
		enemy.next_attack_time = simulation.time + attack_interval +
			(random * 2.0 - 1.0) * attack_interval_variation;
	}

	// At the new position, so the blast and the target match what this frame draws
	const float aim_distance = distance(enemy.position, simulation.aim_point);

	if ((simulation.casts & blast_cast) != 0 && aim_distance < blast_radius)
	{
		take_hit(enemy, index, blast_seed, simulation.blast_damage);
	}

	// Killed by the blast, it can no longer be the target
	if (enemy.health > 0.0 && aim_distance < pick_radius)
	{
		const uint quantized_distance = uint(aim_distance / pick_radius * 65535.0);
		InterlockedMin(combat[0].target_key, (quantized_distance << 16) | index);
	}

	enemies[index] = enemy;
}

// Dispatched as a single thread after update_main, once every enemy has bid to be the target
[shader("compute")]
[numthreads(1, 1, 1)]
void strike_main()
{
	const uint target_key = combat[0].target_key;
	if (target_key == no_target)
	{
		return;
	}

	const uint index = target_key & 0xFFFF;

	EnemyState enemy = enemies[index];
	take_hit(enemy, index, strike_seed, simulation.strike_damage);
	enemies[index] = enemy;
}

// Dispatched as a single thread after every hit of the frame, adds their damage to the total
[shader("compute")]
[numthreads(1, 1, 1)]
void tally_main()
{
	CombatState state = combat[0];
	if (state.frame_damage == 0)
	{
		return;
	}

	// Starts again from 0 once the last total has faded out
	if (simulation.time - state.last_total_hit_time >= total_hold_time + total_fade_time)
	{
		state.total_damage = 0;
	}

	// Stops at the largest value instead of wrapping around to a small one
	state.previous_total_damage = state.total_damage;
	state.total_damage += min(state.frame_damage, 0xFFFFFFFF - state.total_damage);
	state.last_total_hit_time = simulation.time;

	combat[0] = state;
}
