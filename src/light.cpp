#include "quokka/light.hpp"

#include <cmath> // std::cos, std::sin

namespace quokka
{
	glm::vec3 DirectionalLight::direction() const noexcept
	{
		const float azimuth_radians { glm::radians(azimuth) };
		const float elevation_radians { glm::radians(elevation) };

		// The horizontal part shrinks as the light rises, so the result stays unit length
		const float horizontal_length { std::cos(elevation_radians) };

		return {
			horizontal_length * std::sin(azimuth_radians),
			std::sin(elevation_radians),
			horizontal_length * std::cos(azimuth_radians)
		};
	}
}
