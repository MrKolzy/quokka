#include "frame.slang"

// Must match DamageNumber in damage_numbers.hpp
struct DamageNumber
{
	float3 position;
	float spawn_time;
	uint value;
	uint is_critical;
};

// A ring of the latest ones, must match damage_number_binding in graphics_pipeline.cpp
[[vk::binding(3, 0)]]
StructuredBuffer<DamageNumber> damage_numbers;

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

// The digits 0 to 9, each as 7 rows from the top, with its leftmost pixel in the highest bit
static const uint font_rows[70] = {
	0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E,
	0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E,
	0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F,
	0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E,
	0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02,
	0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E,
	0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E,
	0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08,
	0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E,
	0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C
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

	// The same for the whole digit, so they aren't interpolated
	nointerpolation uint digit;
	nointerpolation float4 color;
};

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
	// Outside the clip volume, so the triangles of hidden digits are dropped
	VertexOutput output;
	output.position = float4(2.0, 2.0, 2.0, 1.0);
	output.cell_coordinates = float2(0.0, 0.0);
	output.digit = 0;
	output.color = float4(0.0, 0.0, 0.0, 0.0);

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

	const float4 anchor = mul(frame.projection, mul(frame.view, float4(number.position, 1.0)));

	// Behind the camera
	if (anchor.w <= 0.0)
	{
		return output;
	}

	const float life = age / lifetime;
	const bool is_critical = number.is_critical != 0;

	// Starts bigger and settles within the first tenth of a second
	const float pop = 1.0 + 0.6 * saturate(1.0 - age / 0.1);
	const float scale = pixel_scale * pop * (is_critical ? critical_scale : 1.0);

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
	output.digit = (value / power_of_ten(count - 1 - digit_slot)) % 10;

	// Fades out over the last 30 percent of its life
	const float alpha = 1.0 - smoothstep(0.7, 1.0, life);
	output.color = float4(is_critical ? critical_color : normal_color, alpha);

	return output;
}

bool is_pixel_lit(uint digit, int x, int y)
{
	if (x < 0 || y < 0 || x >= int(glyph_width) || y >= int(glyph_height))
	{
		return false;
	}

	const uint row = font_rows[digit * glyph_height + uint(y)];

	return ((row >> (glyph_width - 1 - uint(x))) & 1) != 0;
}

// Lit pixels take the number's color, the ones around them a black outline that keeps it
// readable over any background, and the rest are discarded
[shader("fragment")]
float4 frag_main(VertexOutput input) : SV_Target
{
	// The outline ring around the glyph is at -1 and at the glyph's width or height
	const int2 pixel = int2(floor(input.cell_coordinates * float2(cell_width, cell_height))) - 1;

	if (is_pixel_lit(input.digit, pixel.x, pixel.y))
	{
		return input.color;
	}

	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			if (is_pixel_lit(input.digit, pixel.x + x, pixel.y + y))
			{
				return float4(0.0, 0.0, 0.0, input.color.a);
			}
		}
	}

	discard;

	// Never reached, but Slang doesn't treat discard as leaving the function
	return float4(0.0, 0.0, 0.0, 0.0);
}
