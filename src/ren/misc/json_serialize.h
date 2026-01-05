#pragma once


#include <ren/types.h>




// #include <json/json.hpp>
// #include <ren/misc/json_serialize.h>

// Wrap NLOHMANN_DEFINE_TYPE_INTRUSIVE
#define JSON_SERIALIZE(type, ...) NLOHMANN_DEFINE_TYPE_INTRUSIVE(type, __VA_ARGS__)



#define __WRITE_JSON_FIELD(field) j[#field] = this->field;
#define TO_JSON(T, ...) \
  nlohmann::json toJson() const { json j; j["_T"] = #T; NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(__WRITE_JSON_FIELD, __VA_ARGS__)); return j; }

#define JSON_SERIALIZE_ENUM(ENUM_TYPE, ...)                                                                                                         \
  inline auto getEnumMap(ENUM_TYPE e) {                                                                                                             \
    static const std::map<ENUM_TYPE, std::string> m = __VA_ARGS__;                                                                                  \
    return m;                                                                                                                                       \
  }                                                                                                                                                 \
  template <typename BasicJsonType>                                                                                                                 \
  inline void to_json(BasicJsonType& j, const ENUM_TYPE& e) {                                                                                       \
    static_assert(std::is_enum<ENUM_TYPE>::value, #ENUM_TYPE " must be an enum!");                                                                  \
    static const std::pair<ENUM_TYPE, BasicJsonType> m[] = __VA_ARGS__;                                                                             \
    auto it =                                                                                                                                       \
        std::find_if(std::begin(m), std::end(m), [e](const std::pair<ENUM_TYPE, BasicJsonType>& ej_pair) -> bool { return ej_pair.first == e; });   \
    j = ((it != std::end(m)) ? it : std::begin(m))->second;                                                                                         \
  }                                                                                                                                                 \
  template <typename BasicJsonType>                                                                                                                 \
  inline void from_json(const BasicJsonType& j, ENUM_TYPE& e) {                                                                                     \
    static_assert(std::is_enum<ENUM_TYPE>::value, #ENUM_TYPE " must be an enum!");                                                                  \
    static const std::pair<ENUM_TYPE, BasicJsonType> m[] = __VA_ARGS__;                                                                             \
    auto it =                                                                                                                                       \
        std::find_if(std::begin(m), std::end(m), [&j](const std::pair<ENUM_TYPE, BasicJsonType>& ej_pair) -> bool { return ej_pair.second == j; }); \
    e = ((it != std::end(m)) ? it : std::begin(m))->first;                                                                                          \
  }

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

  // glm::quat serialization
  template <>
  struct adl_serializer<glm::quat> {
    static void to_json(json& j, const glm::quat& q) { j = json{q.x, q.y, q.z, q.w}; }
    static void from_json(const json& j, glm::quat& q) {
      j.at(0).get_to(q.x);
      j.at(1).get_to(q.y);
      j.at(2).get_to(q.z);
      j.at(3).get_to(q.w);
    }
  };

  // Separate concepts for serialization and deserialization
  template <typename T>
  concept HasToJson = requires(const T& ct) {
    { ct.toJson() } -> std::convertible_to<json>;
  };

  template <typename T>
  concept HasFromJson = requires(const json& j) {
    { T::fromJson(j) } -> std::convertible_to<T>;
  };

  // Specialization for any type with at least toJson()
  template <typename T>
    requires HasToJson<T>
  struct adl_serializer<T> {
    static void to_json(json& j, const T& value) { j = value.toJson(); }

    // Only define from_json if fromJson() exists
    static void from_json(const json& j, T& value)
      requires HasFromJson<T>
    {
      value = T::fromJson(j);
    }
  };

}  // namespace nlohmann