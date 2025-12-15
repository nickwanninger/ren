#pragma once

#include <ren/types.h>

namespace ren::rhi {

  // Note: this is a very incomplete RHI, and i'm not sure i'll ever get around
  // to finishing it.  It's here mostly for thinking about abstracting in the
  // future.



  // Maps to VkBufferUsageFlags on Vulkan
  enum BufferType {
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    StorageBuffer,
  };

  // The RHI in Ren is an abstraction over the low level graphics API.
  class Buffer {
   public:
    struct Desc {};
  };


  // Maps to VkImageType on Vulkan
  enum class TextureType : u8 {
    Texture2D,
    Texture3D,
  };


  enum class TextureFormat : u8 {
    // High level concepts, mapped to actual formats in the backend.
    Depth,         // Whichever depth format is optimal for the GPU
    RGBA8,         // 8-bit per channel RGBA
    RGBA16F,       // 16-bit float per channel RGBA
    RGBA32F,       // 32-bit float per channel RGBA
    RGBA8_SRGB,    // 8-bit per channel sRGB RGBA
    RGBA16F_SRGB,  // 16-bit float per channel sRGB RGBA
    RGBA32F_SRGB,  // 32-bit float per channel sRGB RGBA
  };

  // This is a VkImage and its VkImageView in vulkan.
  class Texture {
   public:
    struct Desc {
      TextureType type = TextureType::Texture2D;
      u32 width = 1;
      u32 height = 1;
      u32 depth = 1;
      u32 mipLevels = 1;
      u32 arrayLayers = 1;
      VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
      VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    };
  };

  // Maps to VkSampler
  class Sampler {
   public:
    struct Desc {
      //
    };

    // - filter modes
    // - address modes
    // - anisotropic filtering
    // - compare modes
  };



  enum class ShaderStage : u8 {
    Vertex,
    Pixel,
    Compute,
  };

  // A single shader stage, e.g., vertex, pixel, compute, compiled from some source.
  class ShaderModule {
   public:
    ShaderModule(ShaderStage stage)
        : stage(stage) {}
    virtual ~ShaderModule() = default;

    inline ShaderStage getStage() const { return stage; }

   private:
    ShaderStage stage;
  };

  // A compiled shader program consisting of multiple shader stages.
  class ShaderProgram {
   public:
   private:
    std::vector<ref<ShaderModule>> stages;
  };

  // A shader object represents an instance of a shader program with specific
  // bindings. These are used to bind resources to the GPU for rendering or
  // compute. They aren't meant to stick around for long periods of time.
  class ShaderObject {};

  // Maps to VkCommandBuffer
  class CommandBuffer {};


  // An instance of a physical GPU device. This is the main interface to the GPU, and the primary
  // method by which resources are allocated.
  class Device {
   public:
    virtual ref<ren::rhi::Buffer> createBuffer(const Buffer::Desc &desc) = 0;
    virtual ref<ren::rhi::Texture> createTexture(const Texture::Desc &desc) = 0;
    virtual ref<ren::rhi::Sampler> createSampler(const Sampler::Desc &desc) = 0;
    // ... etc.
  };



}  // namespace ren::rhi