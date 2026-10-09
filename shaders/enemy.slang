// Included by every shader that reads the enemies or their combat, so they share one definition

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

// Must match CombatState in enemy_simulation.cpp
struct CombatState
{
	// Cleared before every update
	// The enemy closest to the aim point, distance in the high 16 bits and index in the low 16,
	// so the smallest key wins an atomic minimum, or no_target when nobody is close enough
	uint target_key;

	// Cleared before every update, the damage of this frame's hits
	uint frame_damage;

	// Kept from frame to frame, how many numbers were ever spawned, so modulo the capacity
	// it's where the next one goes in the ring
	uint spawned_number_count;

	// Kept from frame to frame, the total damage at the top of the screen
	// What it was before the latest hit, so it can count up from there, and when that hit was,
	// in seconds since the start
	uint total_damage;
	uint previous_total_damage;
	float last_total_hit_time;
};

static const uint no_target = 0xFFFFFFFF;

// Must match DamageNumber in damage_numbers.hpp
struct DamageNumber
{
	// The enemy it belongs to, whose position the shader reads every frame
	uint enemy_index;

	// In seconds, on the same clock as the time in the frame uniforms
	float spawn_time;

	uint value;

	// 0 or 1, since a bool has no fixed size in a shader buffer
	uint is_critical;
};

// Must match DamageNumbers::capacity, a power of 2 so the count can wrap around
static const uint damage_number_capacity = 16384;

// In seconds, the total stays fully visible until this long after the last hit, then fades out
// and starts again from 0, must match total_hold_time and total_fade_time in damage_numbers.cpp
static const float total_hold_time = 5.0;
static const float total_fade_time = 1.5;

// In world units around the aim point, shared by the simulation, which hits everyone inside,
// and the ground, which marks the area before the blast
static const float blast_radius = 3.0;

// True for the enemy whose index is in the key
bool is_target(uint target_key, uint index)
{
	return target_key != no_target && (target_key & 0xFFFF) == index;
}
