#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <fmt/core.h>
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>

namespace ren {

  namespace {
    struct ImageBarrierInfo {
      VkPipelineStageFlags stage;
      VkAccessFlags access;
      VkImageLayout layout;
    };

    static ImageBarrierInfo getImageBarrierInfoForAccess(GraphAccess access) {
      ImageBarrierInfo info;

      switch (access) {
        case GraphAccess::RenderTarget:
          info.stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
          info.access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          break;
        case GraphAccess::DepthTarget:
          info.stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
          info.access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
          break;
        case GraphAccess::FragmentShaderRead:
          info.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
        case GraphAccess::VertexShaderRead:
          info.stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
        case GraphAccess::ComputeShaderRead:
          info.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
        case GraphAccess::ComputeShaderWrite:
          info.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_GENERAL;
          break;
        case GraphAccess::ComputeShaderReadWrite:
          info.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_GENERAL;
          break;
        case GraphAccess::ShaderRead:
          info.stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
          // No default to get compiler warning on missing cases
      }

      return info;
    }

    // Helper: determine aspect mask based on layout
    static VkImageAspectFlags getAspectMask(VkImageLayout layout) {
      switch (layout) {
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
          return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:
          return VK_IMAGE_ASPECT_COLOR_BIT;
      }
    }
  }  // namespace

  ImageResource::ImageResource(const GraphImageSpec &spec)
      : spec(spec) {
    this->type = GraphResourceType::Image;
    this->definingTask = nullptr;
    this->writeAccess = GraphAccess::ShaderRead;
    this->image = nullptr;
  }

  bool ImageResource::update(RenderGraph &G) {
    auto swapchainSize = G.getSwapchainSize();

    bool isRelativeScale = !(spec.scale.x == 0.0f && spec.scale.y == 0.0f);

    // Determine if we need to rebuild
    bool needsRebuild = false;

    if (image == nullptr) {
      // Image hasn't been allocated yet
      needsRebuild = true;
    } else if (isRelativeScale) {
      // For swapchain-relative images, check if size changed
      u32 expectedWidth = static_cast<u32>(swapchainSize.x * spec.scale.x);
      u32 expectedHeight = static_cast<u32>(swapchainSize.y * spec.scale.y);

      if (expectedWidth < 1) expectedWidth = 1;
      if (expectedHeight < 1) expectedHeight = 1;

      u32 currentWidth = image->getWidth();
      u32 currentHeight = image->getHeight();

      if (currentWidth != expectedWidth || currentHeight != expectedHeight) {
        needsRebuild = true;
      }
    }
    // Fixed-size images are never rebuilt once allocated (handled by null check above)

    if (needsRebuild) {
      buildImage(swapchainSize);
      return true;
    }

    return false;
  }

  void ImageResource::buildImage(glm::uvec2 swapchainSize) {
    bool isRelativeScale = !(spec.scale.x == 0.0f && spec.scale.y == 0.0f);

    u32 width = spec.width;
    u32 height = spec.height;

    if (isRelativeScale) {
      width = static_cast<u32>(swapchainSize.x * spec.scale.x);
      height = static_cast<u32>(swapchainSize.y * spec.scale.y);
    }

    if (width < 1) width = 1;
    if (height < 1) height = 1;

    fmt::println("Allocating/reallocating image resource '{}' with size {}x{}", name, width, height);

    if (image == nullptr) {
      fmt::println("  (was null, allocating new)");
    } else {
      fmt::println("  (swapchain size changed, reallocating)");
    }

    ren::ImageBuilder b(name);

    // Set usage flags based on initial access type
    switch (initialAccess) {
      case GraphAccess::RenderTarget:
        b.setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        break;
      case GraphAccess::DepthTarget:
        b.setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        break;
      default:
        b.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        break;
    }

    b.setWidth(width)
        .setHeight(height)
        .setFormat(spec.format)
        .setTiling(VK_IMAGE_TILING_OPTIMAL)
        .setSamples(VK_SAMPLE_COUNT_1_BIT)
        .setMipLevels(1)
        .setArrayLayers(1)
        .setInitialLayout(VK_IMAGE_LAYOUT_UNDEFINED);

    image = b.build();
  }

  void ImageResource::inspect() const {
    ImGui::Text("Image Details:");
    ImGui::Text("  Format: %u", spec.format);
    ImGui::Text("  Scale: (%.2f, %.2f)", spec.scale.x, spec.scale.y);

    if (spec.scale.x != 0 || spec.scale.y != 0) {
      ImGui::Text("  Relative to swapchain");
    } else {
      ImGui::Text("  Absolute size: %u x %u", spec.width, spec.height);
    }

    if (image) {
      ImGui::Text("  Allocated size: %u x %u", image->getWidth(), image->getHeight());
    } else {
      ImGui::TextDisabled("  (not yet allocated)");
    }
  }

  void ImageResource::emitBarrier(GraphRunContext &ctx, GraphAccess fromAccess,
                                  GraphAccess toAccess) {
    if (!image) {
      return;  // Image not yet allocated
    }

    const auto fromInfo = getImageBarrierInfoForAccess(fromAccess);
    const auto toInfo = getImageBarrierInfoForAccess(toAccess);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = fromInfo.access;
    barrier.dstAccessMask = toInfo.access;
    barrier.oldLayout = fromInfo.layout;
    barrier.newLayout = toInfo.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->getImage();
    barrier.subresourceRange.aspectMask = getAspectMask(toInfo.layout);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vkCmdPipelineBarrier(ctx.commandBuffer, fromInfo.stage, toInfo.stage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
  }

}  // namespace ren
