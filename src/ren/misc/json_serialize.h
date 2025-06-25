#pragma once


#include <ren/types.h>

// Wrap NLOHMANN_DEFINE_TYPE_INTRUSIVE
#define JSON_SERIALIZE(type, ...) NLOHMANN_DEFINE_TYPE_INTRUSIVE(type, __VA_ARGS__)

// this file provides serialization for a few types that are used
// in the engine (such as glm::vec3, etc.)

namespace nlohmann {


  // glm::vec2 serialization
  template <>
  struct adl_serializer<glm::vec2> {
    static void to_json(json& j, const glm::vec2& v) { j = json{v.x, v.y}; }

    static void from_json(const json& j, glm::vec2& v) {
      j.at(0).get_to(v.x);
      j.at(1).get_to(v.y);
    }
  };

  // glm::vec3 serialization
  template <>
  struct adl_serializer<glm::vec3> {
    static void to_json(json& j, const glm::vec3& v) { j = json{v.x, v.y, v.z}; }

    static void from_json(const json& j, glm::vec3& v) {
      j.at(0).get_to(v.x);
      j.at(1).get_to(v.y);
      j.at(2).get_to(v.z);
    }
  };



  // glm::vec4 serialization
  template <>
  struct adl_serializer<glm::vec4> {
    static void to_json(json& j, const glm::vec4& v) { j = json{v.x, v.y, v.z, v.w}; }

    static void from_json(const json& j, glm::vec4& v) {
      j.at(0).get_to(v.x);
      j.at(1).get_to(v.y);
      j.at(2).get_to(v.z);
      j.at(3).get_to(v.w);
    }
  };


  // glm::mat4 serialization
  template <>
  struct adl_serializer<glm::mat4> {
    static void to_json(json& j, const glm::mat4& m) { j = json{m[0], m[1], m[2], m[3]}; }
    static void from_json(const json& j, glm::mat4& m) {
      for (int i = 0; i < 4; ++i) {
        j.at(i).get_to(m[i]);
      }
    }
  };
}  // namespace nlohmann