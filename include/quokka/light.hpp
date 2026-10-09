#pragma once

#include <glm/glm.hpp>

namespace quokka
{
	struct DirectionalLight
	{
		// In degrees, the azimuth turns around the world up axis from +z toward +x,
		// and the elevation is the height above the horizon
		float azimuth { -50.0f };
		float elevation { 55.0f };

		glm::vec3 color { 1.0f, 1.0f, 1.0f };

		// Reaches every surface equally, so the ones facing away from the light aren't black
		float ambient_strength { 0.15f };

		// Higher shininess makes the highlights smaller and sharper, and the strength dims them
		float specular_strength { 0.3f };
		float shininess { 32.0f };

		// Unit length, pointing from the surfaces toward the light
		[[nodiscard]] glm::vec3 direction() const noexcept;
	};
}
