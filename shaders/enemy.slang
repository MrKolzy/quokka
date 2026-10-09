// Included by every shader that reads the enemies, so they share one definition

// Must match EnemyState in enemy_simulation.hpp
struct EnemyState
{
	// On the ground, as world x and z
	float2 position;

	// In radians around the world up axis, 0 faces +z and positive values turn toward +x
	float yaw;

	// 0 until the target comes close or hits it, then 1 for good, which is what pulling means
	uint is_chasing;

	// Dead at 0 or below, and dead enemies neither move nor push the others
	float health;

	// In seconds since the start, for the flash, and for dead enemies the moment they died
	float last_hit_time;
};

// Rewritten by the simulation every frame, its size must match combat_size in enemy_simulation.cpp
struct CombatState
{
	// The enemy closest to the aim point, distance in the high 16 bits and index in the low 16,
	// so the smallest key wins an atomic minimum, or no_target when nobody is close enough
	uint target_key;
};

static const uint no_target = 0xFFFFFFFF;

// In world units around the aim point, shared by the simulation, which hits everyone inside,
// and the ground, which marks the area before the blast
static const float blast_radius = 3.0;

// True for the enemy whose index is in the key
bool is_target(uint target_key, uint index)
{
	return target_key != no_target && (target_key & 0xFFFF) == index;
}
