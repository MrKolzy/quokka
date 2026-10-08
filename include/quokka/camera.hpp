#pragma once

#include <glm/glm.hpp>

namespace quokka
{
	class Camera
	{
	public:
		// The target must not be straight above or below the position
		Camera(const glm::vec3& position, const glm::vec3& target) noexcept;

		[[nodiscard]] glm::mat4 view_matrix() const noexcept;
		[[nodiscard]] glm::mat4 projection_matrix(float aspect_ratio) const noexcept;

	private:
		[[nodiscard]] glm::vec3 forward() const noexcept;

		glm::vec3 position_;

		// In radians, yaw turns around the world up axis and pitch looks above the horizon
		float yaw_ {};
		float pitch_ {};
	};
}
