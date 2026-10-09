// Included by every shader that reads the enemies, so they share one definition

// Must match EnemyState in enemy_simulation.hpp
struct EnemyState
{
	// On the ground, as world x and z
	float2 position;

	// In radians around the world up axis, 0 faces +z and positive values turn toward +x
	float yaw;

	// 0 until the target comes close, then 1 for good, which is what pulling them means
	uint is_chasing;
};
