#pragma once
#include <ren/types.h>
#include <ren/misc/json_serialize.h>
#include <glm/gtc/type_precision.hpp>
#include <glm/gtc/packing.hpp>

namespace ren {

  typedef glm::vec<3, _Float16, glm::lowp> lowp_vec3;

  // TEMPORARY VERTEX TYPE
  struct Vertex {
    glm::vec3 pos;
    // glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;    // Tangent vector for normal mapping
    glm::vec3 bitangent;  // Bitangent vector for normal mapping

    JSON_SERIALIZE(Vertex, pos, normal, texCoord);

    Vertex(glm::vec3 pos = glm::vec3(0, 0, 0), glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f),
           glm::vec2 texCoord = glm::vec2(0.0f, 0.0f))
        : pos(pos)
        , normal(normal)
        , texCoord(texCoord) {}




    static VkVertexInputBindingDescription getBindingDesc() {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      return bindingDescription;
    }
    static std::vector<VkVertexInputAttributeDescription> getAttrDescs() {
      std::vector<VkVertexInputAttributeDescription> attrs;

      // Vertex::pos
      attrs.push_back(VkVertexInputAttributeDescription{
          .location = 0,
          .binding = 0,
          .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(Vertex, pos),
      });

      // Vertex::color
      // attrs.push_back(VkVertexInputAttributeDescription{
      //     .binding = 0,
      //     .location = 1,
      //     .format = VK_FORMAT_R32G32B32_SFLOAT,
      //     .offset = offsetof(Vertex, color),
      // });


      // Vertex::normal
      attrs.push_back(VkVertexInputAttributeDescription{
          .location = 1,
          .binding = 0,
          .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(Vertex, normal),
      });

      // Vertex::texCoord
      attrs.push_back(VkVertexInputAttributeDescription{
          .location = 2,
          .binding = 0,
          .format = VK_FORMAT_R32G32_SFLOAT,
          .offset = offsetof(Vertex, texCoord),
      });


      auto BTFormat = VK_FORMAT_R32G32B32_SFLOAT;
      // Vertex::tangent
      attrs.push_back(VkVertexInputAttributeDescription{
          .location = 3,
          .binding = 0,
          .format = BTFormat,
          .offset = offsetof(Vertex, tangent),
      });

      // Vertex::bitangent
      attrs.push_back(VkVertexInputAttributeDescription{
          .location = 4,
          .binding = 0,
          .format = BTFormat,
          .offset = offsetof(Vertex, bitangent),
      });

      return attrs;
    }
  };


}  // namespace ren
