#pragma once

// Heavy headers that rarely change, compiled once and reused by every .cpp
// Never include project headers here, or every change to them would rebuild it

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <print> // std::println
