#include "enemy.slang"
#include "frame.slang"

// Must match DamageNumber in damage_numbers.hpp
struct DamageNumber
{
	uint enemy_index;
	float spawn_time;
	uint value;
	uint is_critical;
};

// Read for the position of the enemy each number belongs to, must match enemy_binding
// in graphics_pipeline.cpp
[[vk::binding(2, 0)]]
StructuredBuffer<EnemyState> enemies;

// A ring of the latest ones, must match damage_number_binding in graphics_pipeline.cpp
[[vk::binding(3, 0)]]
StructuredBuffer<DamageNumber> damage_numbers;

// Must match TotalDamageConstants in damage_numbers.cpp
struct TotalDamageConstants
{
	uint value;

	// What it counts up from after the last hit
	uint previous_value;

	// In seconds, on the same clock as the time in the frame uniforms
	float last_hit_time;
};

[[vk::push_constant]]
ConstantBuffer<TotalDamageConstants> total;

// Above the enemy's head, since the viking room is about a unit tall
static const float anchor_height = 1.2;

// In seconds, must match damage_number_lifetime in damage_numbers.cpp
static const float lifetime = 1.0;

// Must match max_digits in damage_numbers.cpp, so values go up to 999999
static const uint max_digits = 6;
static const uint max_value = 999999;

// A 5x7 pixel font, drawn in cells with one more pixel on every side for the outline
static const uint glyph_width = 5;
static const uint glyph_height = 7;
static const uint cell_width = glyph_width + 2;
static const uint cell_height = glyph_height + 2;

// The digits are glyphs 0 to 9, followed by only the ones the total's label needs
static const uint glyph_a = 10;
static const uint glyph_d = 11;
static const uint glyph_e = 12;
static const uint glyph_g = 13;
static const uint glyph_l = 14;
static const uint glyph_m = 15;
static const uint glyph_o = 16;
static const uint glyph_t = 17;
static const uint glyph_colon = 18;
static const uint glyph_count = 19;

// Not in the font, since its cell is left empty
static const uint glyph_space = glyph_count;

// Each glyph as 7 rows from the top, with its leftmost pixel in the highest bit
static const uint font_rows[glyph_count * glyph_height] = {
	0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E,
	0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E,
	0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F,
	0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E,
	0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02,
	0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E,
	0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E,
	0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08,
	0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E,
	0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C,
	0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11,
	0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E,
	0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F,
	0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F,
	0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11,
	0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E,
	0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00
};

// Screen pixels per font pixel, the same at any distance from the camera
static const float pixel_scale = 2.0;

// Critical hits are bigger and yellow, like in most games
static const float critical_scale = 1.5;
static const float3 normal_color = float3(1.0, 1.0, 1.0);
static const float3 critical_color = float3(1.0, 0.75, 0.05);

// In screen pixels, how far a number rises over its life and how far it can drift sideways
static const float rise_distance = 40.0;
static const float max_drift = 15.0;

// TOTAL DAMAGE, then a colon and a space before the digits
static const uint label_length = 14;
static const uint label_glyphs[label_length] = {
	glyph_t, glyph_o, glyph_t, glyph_a, glyph_l, glyph_space,
	glyph_d, glyph_a, glyph_m, glyph_a, glyph_g, glyph_e, glyph_colon, glyph_space
};

// Must match total_glyph_count in damage_numbers.cpp, so any uint fits
static const uint max_total_digits = 10;

// In seconds, must match total_hold_time and total_fade_time in damage_numbers.cpp
// Fully visible until this long after the last hit, then it fades out
static const float total_hold_time = 5.0;
static const float total_fade_time = 1.5;

// In seconds, how long it takes to count up to the value after a hit
static const float count_up_time = 0.4;

// Bigger than the damage numbers, and its top edge this many screen pixels below the screen's
static const float total_pixel_scale = 3.0;
static const float total_top_margin = 24.0;

// In screen pixels, how far it rises while it fades out
static const float total_rise_distance = 20.0;

// The value takes the critical color, so it stands out from its label
static const float3 label_color = normal_color;
static const float3 total_color = critical_color;

// The two triangles of a quad, from 0 to 1 with y up
static const float2 quad_corners[6] = {
	float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
	float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

struct VertexOutput
{
	float4 position : SV_Position;

	// From 0 to 1 across the cell, starting at its top left corner
	float2 cell_coordinates;

	// The same for the whole glyph, so they aren't interpolated
	nointerpolation uint glyph;
	nointerpolation float4 color;
};

// Outside the clip volume, so the triangles of hidden glyphs are dropped
VertexOutput hidden_vertex()
{
	VertexOutput output;
	output.position = float4(2.0, 2.0, 2.0, 1.0);
	output.cell_coordinates = float2(0.0, 0.0);
	output.glyph = 0;
	output.color = float4(0.0, 0.0, 0.0, 0.0);

	return output;
}

// Starts bigger and settles within the first tenth of a second
float pop_scale(float age)
{
	return 1.0 + 0.6 * saturate(1.0 - age / 0.1);
}

uint digit_count(uint value)
{
	uint count = 1;
	while (value >= 10)
	{
		value /= 10;
		++count;
	}

	return count;
}

uint power_of_ten(uint exponent)
{
	uint result = 1;
	for (uint index = 0; index < exponent; ++index)
	{
		result *= 10;
	}

	return result;
}

// A different value from 0 to 1 for each seed, the PCG hash
float random(uint seed)
{
	const uint state = seed * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	word = (word >> 22u) ^ word;

	return float(word) / 4294967296.0;
}

// Each instance is one number and each group of 6 vertices one of its digits
[shader("vertex")]
VertexOutput vert_main(
	uint vertex_index : SV_VulkanVertexID,
	uint instance_index : SV_VulkanInstanceID)
{
	VertexOutput output = hidden_vertex();

	const DamageNumber number = damage_numbers[instance_index];
	const float age = frame.time - number.spawn_time;
	const uint value = min(number.value, max_value);
	const uint count = digit_count(value);
	const uint digit_slot = vertex_index / 6;

	// Expired, not spawned yet, or a digit this value doesn't have
	if (age < 0.0 || age >= lifetime || digit_slot >= count)
	{
		return output;
	}

	// Follows the enemy while it moves, the way nameplates do
	const float2 ground_position = enemies[number.enemy_index].position;
	const float3 position = float3(ground_position.x, anchor_height, ground_position.y);
	const float4 anchor = mul(frame.projection, mul(frame.view, float4(position, 1.0)));

	// Behind the camera
	if (anchor.w <= 0.0)
	{
		return output;
	}

	const float life = age / lifetime;
	const bool is_critical = number.is_critical != 0;

	const float scale = pixel_scale * pop_scale(age) * (is_critical ? critical_scale : 1.0);

	// Rises fast at first and slows down, and drifts sideways so overlapping numbers separate
	const float rise = rise_distance * (1.0 - (1.0 - life) * (1.0 - life));
	const uint seed = instance_index ^ asuint(number.spawn_time);
	const float drift = (random(seed) * 2.0 - 1.0) * max_drift;

	// The glyph plus a pixel of gap between digits, and the whole number centered on its anchor
	const float advance = float(glyph_width + 1);
	const float number_width = advance * float(count) - 1.0;
	const float2 cell_origin = float2(
		advance * float(digit_slot) - number_width / 2.0 - 1.0,
		-float(cell_height) / 2.0);

	const float2 corner = quad_corners[vertex_index % 6];
	const float2 offset = (cell_origin + corner * float2(cell_width, cell_height)) * scale +
		float2(drift, rise);

	// The flipped viewport makes clip space y point up, the same as these offsets
	output.position = float4(anchor.xy / anchor.w + offset * 2.0 / frame.viewport_size, 0.0, 1.0);
	output.cell_coordinates = float2(corner.x, 1.0 - corner.y);
	output.glyph = (value / power_of_ten(count - 1 - digit_slot)) % 10;

	// Fades out over the last 30 percent of its life
	const float alpha = 1.0 - smoothstep(0.7, 1.0, life);
	output.color = float4(is_critical ? critical_color : normal_color, alpha);

	return output;
}

// The damage of every hit since it appeared, at the top of the screen with the same pop as
// the damage numbers, and each group of 6 vertices is one glyph of its label or its value
[shader("vertex")]
VertexOutput total_vert_main(uint vertex_index : SV_VulkanVertexID)
{
	VertexOutput output = hidden_vertex();

	// Every hit restarts the pop, the count up and the wait before fading out
	const float idle_time = frame.time - total.last_hit_time;
	if (idle_time < 0.0 || idle_time >= total_hold_time + total_fade_time)
	{
		return output;
	}

	// Counts fast at first and slows down as it reaches the value
	const float progress = saturate(idle_time / count_up_time);
	const float eased_progress = 1.0 - (1.0 - progress) * (1.0 - progress);
	const uint value = total.previous_value +
		uint(float(total.value - total.previous_value) * eased_progress);

	const uint count = digit_count(value);
	const uint line_length = label_length + count;
	const uint glyph_slot = vertex_index / 6;

	if (glyph_slot >= line_length)
	{
		return output;
	}

	const bool is_label = glyph_slot < label_length;
	const uint glyph = is_label
		? label_glyphs[glyph_slot]
		: (value / power_of_ten(line_length - 1 - glyph_slot)) % 10;

	if (glyph == glyph_space)
	{
		return output;
	}

	// From 0 to 1 while it fades out, and it rises at the same time like a damage number
	const float fade = smoothstep(total_hold_time, total_hold_time + total_fade_time, idle_time);
	const float scale = total_pixel_scale * pop_scale(idle_time);

	// Laid out like a damage number, the whole line centered on its anchor
	const float advance = float(glyph_width + 1);
	const float line_width = advance * float(line_length) - 1.0;
	const float2 cell_origin = float2(
		advance * float(glyph_slot) - line_width / 2.0 - 1.0,
		-float(cell_height) / 2.0);

	const float2 corner = quad_corners[vertex_index % 6];
	const float2 offset = (cell_origin + corner * float2(cell_width, cell_height)) * scale +
		float2(0.0, total_rise_distance * fade);

	// The center of the line without the pop, so it grows around that point
	// The flipped viewport puts the top of the screen at clip space y 1
	const float anchor_distance = total_top_margin + float(cell_height) * total_pixel_scale / 2.0;
	const float2 anchor = float2(0.0, 1.0 - anchor_distance * 2.0 / frame.viewport_size.y);

	output.position = float4(anchor + offset * 2.0 / frame.viewport_size, 0.0, 1.0);
	output.cell_coordinates = float2(corner.x, 1.0 - corner.y);
	output.glyph = glyph;
	output.color = float4(is_label ? label_color : total_color, 1.0 - fade);

	return output;
}

bool is_pixel_lit(uint glyph, int x, int y)
{
	if (x < 0 || y < 0 || x >= int(glyph_width) || y >= int(glyph_height))
	{
		return false;
	}

	const uint row = font_rows[glyph * glyph_height + uint(y)];

	return ((row >> (glyph_width - 1 - uint(x))) & 1) != 0;
}

// Lit pixels take the number's color, the ones around them a black outline that keeps it
// readable over any background, and the rest are discarded
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	// The outline ring around the glyph is at -1 and at the glyph's width or height
	const int2 pixel = int2(floor(input.cell_coordinates * float2(cell_width, cell_height))) - 1;

	if (is_pixel_lit(input.glyph, pixel.x, pixel.y))
	{
		return input.color;
	}

	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			if (is_pixel_lit(input.glyph,pixel.x + x, pixel.y + y))
			{
				return float4(0.0, 0.0, 0.0, input.color.a);
			}
		}
	}

	discard;

	// Never reached, but Slang doesn't treat discard as leaving the function
	return float4(0.0, 0.0, 0.0, 0.0);
}
