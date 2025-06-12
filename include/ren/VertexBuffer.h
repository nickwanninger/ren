#pragma once

#include <ren/Vulkan.h>


namespace ren {


  //
  template <typename VertexType>
  class VertexBuffer : public Buffer {
   public:
    using Vertex = VertexType;
    VertexBuffer(VulkanInstance &vulkan_instance, size_t vertexCount, bool deviceLocal = true,
                 bool hostVisible = false)
        : Buffer(vulkan_instance, size, usage, properties) {
      // Create the vertex buffer
      this->vulkan.create_buffer(size, usage, properties, this->buffer, this->memory);
    }

    // Convenience constructors
    VertexBuffer(VulkanInstance &vulkan, const std::vector<VertexType> &vertices,
                 bool deviceLocal = true);
    VertexBuffer(VulkanInstance &vulkan, const VertexType *vertices, size_t count,
                 bool deviceLocal = true);

    ~VertexBuffer() override = default;

    // Vertex-specific operations
    void updateVertices(const std::vector<VertexType> &vertices, size_t offset = 0) {
      updateVertices(vertices.data(), vertices.size(), offset);
    }
    void updateVertices(const VertexType *vertices, size_t count, size_t offset = 0);
    void updateVertex(const VertexType &vertex, size_t index) {

    }
  };
};  // namespace ren
