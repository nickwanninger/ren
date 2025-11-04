#pragma once


#include <ren/renderer/graph/Handle.h>

namespace ren {


  // Definitions for Graph Resources.
  enum class GraphResourceType : u8 {
    Image,   // Constructed with GraphImageSpec/createImage
    Buffer,  // Not yet implemented. For GPU compute.
  };


  // When accessing a resource, what kind of access is performed. This is used
  // to determine layout transitions and pipeline barriers required for the
  // resource between tasks. For example, a resource written as a RenderTarget
  // in one task, and read as a ShaderRead in another will require a layout
  // transition and appropriate pipeline barriers.  This is a simplified model
  // compared to real graphics APIs, and I'll expand it as needed, but basic
  // functionality should be covered.
  enum class GraphAccess : u8 {
    // The resource is written as a render target (color)
    RenderTarget,
    // The resource is written as a depth target
    DepthTarget,

    // The resource is read in a shader
    VertexShaderRead,
    FragmentShaderRead,
    ComputeShaderRead,


    // The resource is written in a compute shader
    // Note: Writing in fragment or vertex shaders is not supported, as vulkan doesn't.
    ComputeShaderWrite,
    // This is here for completeness, but is not handled in the render graph
    // scheduler, as it only supports RAW depdendencies at the moment, where
    // there is one writer, and many readers. The readers are not expected to be
    // ordered when executed, so a WAW or WAR dependency cannot be properly
    // handled.  This would require more complex scheduling (likely involving
    // something like aliased resources).
    ComputeShaderReadWrite,

    // Generic read in all shaders.
    ShaderRead,
  };


  // The specification for creating an image in the render graph.
  struct GraphImageSpec {
    // Resolution should come from the swapchain size multiplied by this scale factor.
    // If this value is 0, the size is absolute. (.width, .height)
    glm::vec2 scale = glm::vec2(0.0f);

    // Absolute width/height.
    u32 width = 0;
    u32 height = 0;

    VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
  };




  // An operand of a resource in the render graph.
  // This represents a single location where a resource is used as an operand (read), and how it is
  // used. Think about this like an llvm::Use (it's an argument, basically).
  struct GraphOperand {
    GraphHandle valueHandle;
    GraphAccess access;
    GraphResourceType resourceType;

    GraphOperand(GraphHandle h, GraphAccess a, GraphResourceType type)
        : valueHandle(h)
        , access(a)
        , resourceType(type) {}

    std::string toString(void) const;
  };

}  // namespace ren



/// ----------------------------------------- ///

template <>
struct fmt::formatter<ren::GraphResourceType> : fmt::formatter<std::string_view> {
  static const char *toString(ren::GraphResourceType access) {
#define CASE_ENUM_TO_STRING(e) \
  case ren::GraphResourceType::e: return #e;
    switch (access) {
      CASE_ENUM_TO_STRING(Image);
      CASE_ENUM_TO_STRING(Buffer);
    }
#undef CASE_ENUM_TO_STRING
  }

  auto format(ren::GraphResourceType access, fmt::format_context &ctx) const {
    return fmt::formatter<std::string_view>::format(toString(access), ctx);
  }
};

/// ----------------------------------------- ///

template <>
struct fmt::formatter<ren::GraphAccess> : fmt::formatter<std::string_view> {
  static const char *toString(ren::GraphAccess access) {
#define CASE_ENUM_TO_STRING(e) \
  case ren::GraphAccess::e: return #e;
    switch (access) {
      CASE_ENUM_TO_STRING(RenderTarget);
      CASE_ENUM_TO_STRING(DepthTarget);
      CASE_ENUM_TO_STRING(FragmentShaderRead);
      CASE_ENUM_TO_STRING(VertexShaderRead);
      CASE_ENUM_TO_STRING(ComputeShaderRead);
      CASE_ENUM_TO_STRING(ComputeShaderWrite);
      CASE_ENUM_TO_STRING(ComputeShaderReadWrite);
      CASE_ENUM_TO_STRING(ShaderRead);
    }
#undef CASE_ENUM_TO_STRING
  }

  auto format(ren::GraphAccess access, fmt::format_context &ctx) const {
    return fmt::formatter<std::string_view>::format(toString(access), ctx);
  }
};

/// ----------------------------------------- ///