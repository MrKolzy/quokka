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
// The hook lands when the thrown sword arrives, which the CPU times
static const uint blast_cast = 1;
static const uint test_cast = 2;
static const uint hook_cast = 4;

// Must match SimulationConstants in enemy_simulation.cpp
struct SimulationConstants
{
	// On the ground, as world x and z
	float2 target;

	// Capped on the CPU, so a long frame doesn't make the enemies jump
	float delta_seconds;

	uint enemy_count;

	// Unit length on the ground, as world x and z, where the target faces, which is where a hit
	// shoves an enemy standing right on top of it
	float2 facing;

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

	// A world ray, and 0 to keep, 1 to pick, 2 to cycle or 3 to clear the selection
	float3 selection_origin;
	uint selection_mode;
	float3 selection_direction;
	uint selection_padding;

	// Where Whirlwind landed, as world x and z, and the quake's minimum and maximum damage
	float2 quake_center;
	float2 quake_damage;

	// In world units, how far the first seismic wave has spread and how much of that is new this
	// frame, so each wave hits an enemy once as it passes, and 0 when no wave moved
	float quake_front;
	float quake_advance;

	// Per attack on the player, from the level's difficulty
	uint attack_damage;
};

[[vk::push_constant]]
ConstantBuffer<SimulationConstants> simulation;

// In world units, and the grid starts with the enemies 2 units apart
static const float aggro_radius = 8.0;

// Two ground tiles beyond aggro, each tile is 2 world units
static const float selection_radius = aggro_radius + 4.0;
static const float stop_distance = 2.0;
static const float separation_radius = 1.6;

// Per second, how much of the way to the black hole a hooked enemy closes, and in world units
// per second the fastest it's dragged
static const float hook_pull_rate = 10.0;
static const float hook_max_speed = 60.0;

// Once the black hole closes on the player, a hooked enemy lags behind it, so it's let go once
// this close to the player in world units, or this many seconds later if the crowd blocks it
static const float hook_release_distance = 3.0;
static const float hook_catch_up_duration = 1.0;

// In world units per second, slower than the player in player.cpp, so they can be kited and
// gathered, and the walk clip in shader.slang plays faster with it, must match chase_speed there
static const float chase_speed = 5.0;
static const float separation_speed = 4.0;
static const float max_speed = 8.0;

// In world units per second, below it an enemy counts as standing still
static const float min_moving_speed = 0.8;

// In world units per second, the shove a hit gives away from the target, stronger when it kills,
// so the dead fly back, and capped so a pile of hits doesn't launch anyone across the map
static const float blast_knockback = 10.0;
static const float quake_knockback = 14.0;
static const float killing_knockback_scale = 2.0;
static const float max_knockback = 20.0;

// Per second, how fast the shove fades, so it's over in a fraction of a second
static const float knockback_damping = 7.0;

// In world units, a bit past stop_distance, so the enemies that stopped next to the target
// reach it, and only the first ring around it does, since the separation keeps the rest out
static const float attack_range = 2.4;

// Seconds between attacks, each one a little longer or shorter so a crowd doesn't hit in step,
// balanced around the player's health in player.cpp
static const float attack_interval = 1.5;
static const float attack_interval_variation = 0.3;

// The test isn't an ability, so its damage only lives here
static const float2 test_damage = float2(100.0, 999.0);

// So an enemy hit by both abilities in the same frame rolls its damage twice
static const uint blast_seed = 1;
static const uint strike_seed = 2;
static const uint test_seed = 3;
static const uint quake_seed = 4;

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

// True when the front of a seismic wave passed the position this frame
bool is_reached_by_quake(float2 position)
{
	const float quake_distance = distance(position, simulation.quake_center);
	if (quake_distance >= quake_radius)
	{
		return false;
	}

	for (uint wave = 0; wave < quake_wave_count; ++wave)
	{
		const float front = simulation.quake_front - float(wave) * quake_wave_spacing;
		if (quake_distance < front && quake_distance >= front - simulation.quake_advance)
		{
			return true;
		}
	}

	return false;
}

// The hit pulls the enemy however far it is, and shoves it away from the target
void take_hit(
	inout EnemyState enemy,
	uint index,
	uint seed,
	float2 damage_range,
	float knockback_speed)
{
	enemy.health -= float(roll_hit(index, seed, damage_range));
	enemy.last_hit_time = simulation.time;
	enemy.flags |= chasing_flag;
	InterlockedAdd(combat[0].frame_hits, 1);

	// Right on top of the target there's no away, so it goes the way the target faces
	const float2 away = enemy.position - simulation.target;
	const float away_length = length(away);
	const float2 direction = away_length > 1.0e-3 ? away / away_length : simulation.facing;
	const float scale = enemy.health <= 0.0 ? killing_knockback_scale : 1.0;

	enemy.knockback += direction * knockback_speed * scale;
	const float knockback_length = length(enemy.knockback);
	if (knockback_length > max_knockback)
	{
		enemy.knockback *= max_knockback / knockback_length;
	}
}

// Moves the enemy by its shove, which fades over time, also when dead so the body slides
void apply_knockback(inout EnemyState enemy)
{
	enemy.position += enemy.knockback * simulation.delta_seconds;
	enemy.knockback *= exp(-knockback_damping * simulation.delta_seconds);
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
		apply_knockback(enemy);
		enemies[index] = enemy;

		return;
	}

	const float2 to_target = simulation.target - enemy.position;
	const float target_distance = length(to_target);

	if (target_distance < aggro_radius)
	{
		enemy.flags |= chasing_flag;
	}

	const bool is_chasing = (enemy.flags & chasing_flag) != 0;

	// Hooked until the black hole has brought it next to the player
	const float time_since_impact =
		simulation.time - combat[0].hook_time - hook_flight_duration;
	const float lifetime = hook_lifetime(combat[0].hook_point, simulation.target);
	const bool has_arrived =
		time_since_impact >= lifetime && target_distance < hook_release_distance;
	const bool is_hooked = (enemy.flags & hooked_flag) != 0 && !has_arrived &&
		time_since_impact < lifetime + hook_catch_up_duration;
	if (!is_hooked)
	{
		enemy.flags &= ~hooked_flag;
	}

	// The crowd still pushes, so the hooked ones bunch up around the black hole instead of
	// collapsing into one point
	float2 velocity = push * separation_speed;
	float speed_limit = max_speed;

	if (is_hooked)
	{
		const float2 center =
			hook_center(combat[0].hook_point, simulation.target, time_since_impact);
		velocity += (center - enemy.position) * hook_pull_rate;
		speed_limit = hook_max_speed;
	}
	else if (is_chasing && target_distance > stop_distance)
	{
		velocity += to_target / target_distance * chase_speed;
	}

	// Facing the target while chasing it, the same convention as rotate_around_up
	if (is_chasing && target_distance > 1.0e-3)
	{
		enemy.yaw = atan2(to_target.x, to_target.y);
	}

	// A crowd pushing from every side could otherwise launch someone
	const float speed = length(velocity);
	if (speed > speed_limit)
	{
		velocity *= speed_limit / speed;
	}

	enemy.position += velocity * simulation.delta_seconds;
	apply_knockback(enemy);

	// Slower drifts from the crowd's pushing don't count, or standing enemies would shuffle
	enemy.flags = length(velocity) > min_moving_speed
		? enemy.flags | moving_flag
		: enemy.flags & ~moving_flag;

	const bool can_attack = simulation.is_target_alive != 0 && is_chasing &&
		distance(enemy.position, simulation.target) < attack_range;
	if (can_attack && simulation.time >= enemy.next_attack_time)
	{
		InterlockedAdd(combat[0].player_damage, simulation.attack_damage);
		enemy.last_attack_time = simulation.time;

		const float random = float(pcg_hash(index ^ asuint(simulation.time)) & 0xFFFF) / 65535.0;
		enemy.next_attack_time = simulation.time + attack_interval +
			(random * 2.0 - 1.0) * attack_interval_variation;
	}

	// At the new position, so the blast matches what this frame draws
	const float enemy_distance = distance(enemy.position, simulation.target);

	// A nova centered on the target
	if ((simulation.casts & blast_cast) != 0 && enemy_distance < blast_radius)
	{
		take_hit(enemy, index, blast_seed, simulation.blast_damage, blast_knockback);
	}

	if (simulation.quake_advance > 0.0 && is_reached_by_quake(enemy.position))
	{
		take_hit(enemy, index, quake_seed, simulation.quake_damage, quake_knockback);
	}

	// The sword lands, hurting the enemy it flew at without a shove, and its black hole hooks
	// everyone around and stops their shoves, so nothing fights the pull
	if ((simulation.casts & hook_cast) != 0)
	{
		const bool is_sword_target = combat[0].hook_index == index;
		if (is_sword_target)
		{
			take_hit(enemy, index, strike_seed, simulation.strike_damage, 0.0);
		}

		if (is_sword_target || distance(enemy.position, combat[0].hook_point) < hook_radius)
		{
			enemy.flags |= hooked_flag | chasing_flag;
			enemy.knockback = float2(0.0, 0.0);
		}
	}

	// After every hit, so the enemies killed this frame no longer count
	if (enemy.health > 0.0)
	{
		InterlockedAdd(combat[0].alive_count, 1);
	}

	enemies[index] = enemy;
}

// A forgiving box around the animated skeleton, the nearest intersection wins
float pick_distance(EnemyState enemy)
{
	const float3 center = float3(enemy.position.x, 1.3, enemy.position.y);
	const float3 origin = simulation.selection_origin - center;
	const float3 direction = simulation.selection_direction;
	const float3 half_size = float3(0.65, 1.3, 0.65);
	float near_distance = 0.0;
	float far_distance = 500.0;

	for (uint axis = 0; axis < 3; ++axis)
	{
		if (abs(direction[axis]) < 1.0e-6)
		{
			if (abs(origin[axis]) > half_size[axis])
			{
				return 1.0e9;
			}
		}
		else
		{
			const float first = (-half_size[axis] - origin[axis]) / direction[axis];
			const float second = (half_size[axis] - origin[axis]) / direction[axis];
			near_distance = max(near_distance, min(first, second));
			far_distance = min(far_distance, max(first, second));
		}
	}

	return near_distance <= far_distance ? near_distance : 1.0e9;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void selection_main()
{
	uint selected = combat[0].selected_index;
	if (selected < simulation.enemy_count && enemies[selected].health <= 0.0)
	{
		selected = no_target;
	}

	if (simulation.selection_mode == 3 || simulation.is_target_alive == 0)
	{
		selected = no_target;
	}
	else if (simulation.selection_mode == 1)
	{
		selected = no_target;
		float nearest = 1.0e9;
		for (uint index = 0; index < simulation.enemy_count; ++index)
		{
			const EnemyState enemy = enemies[index];
			if (enemy.health <= 0.0)
			{
				continue;
			}

			const float hit_distance = pick_distance(enemy);
			if (hit_distance < nearest)
			{
				nearest = hit_distance;
				selected = index;
			}
		}
	}
	else if (simulation.selection_mode == 2)
	{
		uint next = no_target;
		float nearest = selection_radius;
		for (uint step = 0; step < simulation.enemy_count; ++step)
		{
			const uint index = selected == no_target
				? step : (selected + 1 + step) % simulation.enemy_count;
			const EnemyState enemy = enemies[index];
			const float range = distance(enemy.position, simulation.target);
			if (enemy.health <= 0.0 || range > selection_radius)
			{
				continue;
			}

			if (selected != no_target)
			{
				next = index;

				break;
			}

			if (range <= nearest)
			{
				nearest = range;
				next = index;
			}
		}
		selected = next;
	}

	combat[0].selected_index = selected;
}

// Dispatched as a single thread when the sword is thrown, after the selection, and the hit
// itself waits until the sword lands
// The CPU only throws at a selected enemy, but it reads the selection a few frames
// late, so if the enemy died meanwhile the sword still flies to where it last was
[shader("compute")]
[numthreads(1, 1, 1)]
void strike_main()
{
	CombatState state = combat[0];

	const uint selected = state.selected_index;
	const bool is_selected_alive =
		selected < simulation.enemy_count && enemies[selected].health > 0.0;

	state.hook_point = is_selected_alive ? enemies[selected].position : state.selected_position;
	state.hook_time = simulation.time;
	state.hook_index = is_selected_alive ? selected : no_target;

	combat[0] = state;
}

// Dispatched as a single thread after every hit of the frame, adds their damage to the total
[shader("compute")]
[numthreads(1, 1, 1)]
void tally_main()
{
	CombatState state = combat[0];
	state.selected_health = 0.0;
	if (state.selected_index < simulation.enemy_count)
	{
		state.selected_health = max(enemies[state.selected_index].health, 0.0);
		state.selected_position = enemies[state.selected_index].position;
		if (state.selected_health <= 0.0)
		{
			state.selected_index = no_target;
		}
	}
	combat[0] = state;

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
