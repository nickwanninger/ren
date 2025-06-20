#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <iostream>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


// How many frames we want to render in flight at once.
// currently, anything other than 3 crashes.
const int MAX_FRAMES_IN_FLIGHT = 3;

using u8 = uint8_t;
using i8 = int8_t;
using u16 = uint16_t;
using i16 = int16_t;
using u32 = uint32_t;
using i32 = int32_t;
using u64 = uint64_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;



// we want to immediately abort when there is an error. In normal engines this
// would give an error message to the user, or perform a dump of state.
#define VK_CHECK(x)                                               \
  do {                                                            \
    VkResult err = x;                                             \
    if (err) {                                                    \
      std::cout << "Detected Vulkan error: " << err << std::endl; \
      abort();                                                    \
    }                                                             \
  } while (0)



namespace ren {
  struct MeshPushConstants {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };


  inline float randomFloat(float min = 0.0f, float max = 1.0f) {
    return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
  }

  inline glm::vec3 randomDirection() {
    float x = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
    float y = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
    float z = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
    glm::vec3 dir(x, y, z);
    return glm::normalize(dir);
  }
  inline glm::vec3 randomColor(void) {
    // [0, 1] range
    float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    float g = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    float b = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return glm::vec3(r, g, b);
  }

  template <typename T>
  using ref = std::shared_ptr<T>;

  template <typename T, typename... Args>
  constexpr ref<T> makeRef(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
  }

  template <typename T>
  using box = std::unique_ptr<T>;

  template <typename T, typename... Args>
  constexpr box<T> makeBox(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
  }

}  // namespace ren
