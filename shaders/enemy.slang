// Included by every shader that reads the enemies or their combat, so they share one definition

// Must match EnemyState in enemy_simulation.hpp
struct EnemyState
{
	// On the ground, as world x and z
	float2 position;

	// In radians around the world up axis, 0 faces +z and positive values turn toward +x
	float yaw;

	// Bits of chasing_flag and moving_flag, since a bool has no fixed size in a shader buffer
	uint flags;

	// Dead at 0 or below, and dead enemies neither move nor push the others
	float health;

	// In seconds since the start, for the flash, and for dead enemies the moment they died
	float last_hit_time;

	// In seconds since the start, when it can attack the player again and when it last did,
	// which starts its attack animation
	float next_attack_time;
	float last_attack_time;

	// In world units per second, the push of the last hits away from the player, which fades
	float2 knockback;
};

// Set once the target comes close or hits it, and kept for good, which is what pulling means
static const uint chasing_flag = 1;

// Set while it walks, and cleared while it stands still, for its animation
static const uint moving_flag = 2;

// Set while the thrown sword's black hole drags it, and cleared once it lets go
static const uint hooked_flag = 4;

// What every enemy starts with, must match enemy_health in enemy_simulation.cpp
static const float enemy_max_health = 1000.0;

// Must match CombatState in enemy_simulation.cpp
struct CombatState
{
	// Cleared before every update, the damage of this frame's hits on the enemies
	// and of their attacks on the player, which the CPU reads back
	uint frame_damage;
	uint player_damage;

	// Cleared before every update, how many enemies the abilities hit this frame, which the CPU
	// reads back with the damage to the player for the hit-stop and the camera shake
	uint frame_hits;

	// Kept from frame to frame, how many numbers were ever spawned, so modulo the capacity
	// it's where the next one goes in the ring
	uint spawned_number_count;

	// Kept from frame to frame, the total damage at the right of the screen
	// What it was before the latest hit, so it can count up from there, and when that hit was,
	// in seconds since the start
	uint total_damage;
	uint previous_total_damage;
	float last_total_hit_time;

	// Kept from frame to frame, when the last sword was thrown, in seconds since the start
	float hook_time;

	// Persistent selection, which the CPU reads back for the hud, with its position on the ground
	// as world x and z for a sword thrown just as the enemy died
	float2 selected_position;
	uint selected_index;
	float selected_health;

	// Kept from frame to frame, where the last thrown sword flies to as world x and z, and the
	// enemy it hits, or no_target for open ground
	float2 hook_point;
	uint hook_index;

	// Cleared before every update, how many enemies of the level are still alive after it,
	// which the CPU reads back to know when the level is cleared
	uint alive_count;
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

// In world units around the player, shared by the simulation, which hits everyone inside,
// and the ground, which marks the area before the blast, must match blast_radius in abilities.hpp
static const float blast_radius = 4.5;

// In world units from the player, how far away the sword can be thrown, which the ground marks
// when the player tries to throw it without a target in reach, must match strike_range
// in abilities.hpp
static const float strike_range = 12.0;

// Whirlwind's landing sends this many seismic waves out, each this far in world units behind
// the last, spreading this fast in world units per second up to this radius, shared by the
// simulation and the ground, must match the quake constants in abilities.hpp
static const uint quake_wave_count = 3;
static const float quake_wave_spacing = 3.0;
static const float quake_wave_speed = 24.0;
static const float quake_radius = 12.0;

// The thrown sword flies for this long in seconds, then its black hole pulls in every enemy
// within the radius in world units, must match hook_flight_duration and hook_radius
// in abilities.hpp
static const float hook_flight_duration = 0.2;
static const float hook_radius = 8.0;

// In seconds, the black hole holds the enemies where the sword landed, then drags them back to
// the player at this speed in world units per second, taking at least the minimum time
static const float hook_hold_duration = 0.3;
static const float hook_drag_speed = 30.0;
static const float hook_min_drag_duration = 0.4;

// In seconds, longer the farther the sword landed, so any distance is dragged at the same speed
float hook_drag_duration(float2 hook_point, float2 player_position)
{
	return max(hook_min_drag_duration, distance(hook_point, player_position) / hook_drag_speed);
}

// In seconds after the sword lands, when the black hole reaches the player and closes
float hook_lifetime(float2 hook_point, float2 player_position)
{
	return hook_hold_duration + hook_drag_duration(hook_point, player_position);
}

// Where the black hole is, this long in seconds after the sword lands, from where it landed to
// the player, slow at both ends
float2 hook_center(float2 hook_point, float2 player_position, float time_since_impact)
{
	const float drag = saturate((time_since_impact - hook_hold_duration) /
		hook_drag_duration(hook_point, player_position));

	return lerp(hook_point, player_position, drag * drag * (3.0 - 2.0 * drag));
}
