#pragma once

#include "quokka/constants.hpp"

#include <glm/glm.hpp>

#include <array> // std::array
#include <cstdint> // std::uint32_t
#include <span> // std::span

namespace quokka
{
	enum class Difficulty
	{
		easy,
		normal
	};

	[[nodiscard]] constexpr const char* difficulty_name(Difficulty difficulty) noexcept
	{
		return difficulty == Difficulty::easy ? "Easy" : "Normal";
	}

	// Damage per attack, the player's health in player.cpp is balanced around normal
	[[nodiscard]] constexpr std::uint32_t enemy_attack_damage(Difficulty difficulty) noexcept
	{
		return difficulty == Difficulty::easy ? 3 : 6;
	}

	// Enemies standing together in a square grid around the center, as world x and z
	struct EnemyPack
	{
		glm::vec2 center;
		std::uint32_t count;
	};

	struct Level
	{
		const char* name;
		Difficulty difficulty;

		// On the ground as world x and z, and the player starts facing the world origin
		glm::vec2 player_start;

		std::span<const EnemyPack> packs;
	};

	// The smallest side whose square holds the count, so the last row may be incomplete
	[[nodiscard]] constexpr std::uint32_t square_side(std::uint32_t count) noexcept
	{
		std::uint32_t side { 1 };
		while (side * side < count)
		{
			++side;
		}

		return side;
	}

	// In world units between the centers of neighbors in a pack, a bit more than a skeleton's width
	inline constexpr float pack_spacing { 2.0f };

	// In world units from the center of a pack to its outermost enemies along x and z
	[[nodiscard]] constexpr float pack_half_size(std::uint32_t count) noexcept
	{
		return static_cast<float>(square_side(count) - 1) * pack_spacing / 2.0f;
	}

	[[nodiscard]] constexpr std::uint32_t level_enemy_count(const Level& level) noexcept
	{
		std::uint32_t count { 0 };
		for (const EnemyPack& pack : level.packs)
		{
			count += pack.count;
		}

		return count;
	}

	// Small packs on the way to a bigger one, far enough apart to be pulled one at a time,
	// since aggro_radius in enemy_simulation.slang is 8
	inline constexpr std::array level_one_packs {
		EnemyPack { .center { 18.0f, 20.0f }, .count { 4 } },
		EnemyPack { .center { 5.0f, 8.0f }, .count { 6 } },
		EnemyPack { .center { -8.0f, -4.0f }, .count { 9 } },
		EnemyPack { .center { -6.0f, -22.0f }, .count { 9 } },
		EnemyPack { .center { -24.0f, -26.0f }, .count { 16 } }
	};

	// Every enemy in one big square, for the benchmarks
	inline constexpr std::array benchmark_packs {
		EnemyPack { .center { 0.0f, 0.0f }, .count { constants::enemy_count } }
	};

	// Past a corner of the square, out of reach of its enemies, which must stay above
	// aggro_radius in enemy_simulation.slang
	inline constexpr float benchmark_start { pack_half_size(constants::enemy_count) + 8.0f };

	// F3 goes through them in order, and the game starts with the first
	inline constexpr std::array levels {
		Level {
			.name { "Level 1" },
			.difficulty { Difficulty::easy },
			.player_start { 30.0f, 30.0f },
			.packs { level_one_packs }
		},
		Level {
			.name { "Benchmark" },
			.difficulty { Difficulty::normal },
			.player_start { benchmark_start, benchmark_start },
			.packs { benchmark_packs }
		}
	};

	// The enemy buffers hold constants::enemy_count, which every level must fit in
	[[nodiscard]] constexpr bool do_levels_fit() noexcept
	{
		for (const Level& level : levels)
		{
			const std::uint32_t count { level_enemy_count(level) };
			if (count == 0 || count > constants::enemy_count)
			{
				return false;
			}
		}

		return true;
	}

	static_assert(do_levels_fit());
}
