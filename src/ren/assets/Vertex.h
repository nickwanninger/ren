#pragma once
#include <ren/types.h>
#include <ren/misc/json_serialize.h>

namespace ren {


  // TEMPORARY VERTEX TYPE
  struct Vertex {
    glm::vec3 pos;
    // glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 texCoord;

    JSON_SERIALIZE(Vertex, pos, normal, texCoord);

    Vertex(glm::vec3 pos = glm::vec3(0,0,0),  glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f),
           glm::vec2 texCoord = glm::vec2(0.0f, 0.0f))
        : pos(pos), normal(normal), texCoord(texCoord) {
    }




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
          .location = (u32)attrs.size(),
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
          .location = (u32)attrs.size(),
          .binding = 0,
          .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = offsetof(Vertex, normal),
      });

      // Vertex::texCoord
      attrs.push_back(VkVertexInputAttributeDescription{
          .location = (u32)attrs.size(),
          .binding = 0,
          .format = VK_FORMAT_R32G32_SFLOAT,
          .offset = offsetof(Vertex, texCoord),
      });


      return attrs;
    }
  };
}  // namespace ren
