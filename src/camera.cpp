#include "quokka/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm> // std::clamp
#include <cmath> // std::atan2, std::cos, std::hypot, std::sin

namespace quokka
{
	namespace
	{
		// 45 degree vertical field of view
		constexpr float field_of_view { glm::radians(45.0f) };

		constexpr float near_plane { 0.1f };

		// Far enough for big scenes, at the cost of some depth precision in the distance
		constexpr float far_plane { 500.0f };

		constexpr glm::vec3 world_up { 0.0f, 1.0f, 0.0f };

		// At 90 degrees forward would be parallel to world_up, which breaks lookAt
		constexpr float max_pitch { glm::radians(89.0f) };
	}

	// ***

	Camera::Camera(const glm::vec3& position, const glm::vec3& target) noexcept
		: position_ { position }
	{
		// The inverse of forward
		const glm::vec3 direction { target - position };
		yaw_ = std::atan2(direction.x, -direction.z);
		pitch_ = std::atan2(direction.y, std::hypot(direction.x, direction.z));
	}

	glm::mat4 Camera::view_matrix() const noexcept
	{
		return glm::lookAt(position_, position_ + forward(), world_up);
	}

	glm::mat4 Camera::projection_matrix(float aspect_ratio) const noexcept
	{
		return glm::perspective(field_of_view, aspect_ratio, near_plane, far_plane);
	}

	void Camera::move(const glm::vec3& offset) noexcept
	{
		position_ += right() * offset.x + world_up * offset.y + forward() * offset.z;
	}

	void Camera::rotate(float yaw_offset, float pitch_offset) noexcept
	{
		yaw_ += yaw_offset;
		pitch_ = std::clamp(pitch_ + pitch_offset, -max_pitch, max_pitch);
	}

	glm::vec3 Camera::forward() const noexcept
	{
		// Yaw and pitch 0 look toward -z, like a view matrix that is the identity
		return {
			std::cos(pitch_) * std::sin(yaw_),
			std::sin(pitch_),
			-std::cos(pitch_) * std::cos(yaw_)
		};
	}

	glm::vec3 Camera::right() const noexcept
	{
		// Horizontal, so moving sideways never changes the height
		return { std::cos(yaw_), 0.0f, std::sin(yaw_) };
	}
}
