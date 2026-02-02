#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <fmt/core.h>
#include <vulkan/vulkan.h>


#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

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

          // TODO: is this correct? we have attachment.finalLayout set to this
          // in RenderPass.cpp, but I think that is only for the swapchain?
          info.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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
    static VkImageAspectFlags getAspectMask(GraphAccess initialAccess) {
      if (initialAccess == ren::GraphAccess::DepthTarget) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
      } else {
        return VK_IMAGE_ASPECT_COLOR_BIT;
      }
    }
  }  // namespace

  ImageResource::ImageResource(const GraphImageSpec &spec)
      : spec(spec) {
    this->type = GraphResourceType::Image;
    this->image = nullptr;
  }

  bool ImageResource::update(RenderGraph &G) {
    auto swapchainSize = G.getSwapchainSize();

    bool isRelativeScale = spec.relativeScale.isSome();

    // Determine if we need to rebuild
    bool needsRebuild = false;

    if (image == nullptr) {
      // Image hasn't been allocated yet
      needsRebuild = true;
    } else if (isRelativeScale) {
      auto relativeScale = spec.relativeScale.unwrap();
      // For swapchain-relative images, check if size changed
      u32 expectedWidth = static_cast<u32>(swapchainSize.x * relativeScale.x);
      u32 expectedHeight = static_cast<u32>(swapchainSize.y * relativeScale.y);

      if (expectedWidth < 1) {
        expectedWidth = 1;
      }
      if (expectedHeight < 1) {
        expectedHeight = 1;
      }

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
    bool isRelativeScale = spec.relativeScale.isSome();

    assert(spec.relativeScale.isSome() || spec.absoluteSize.isSome());

    u32 width = 0;
    u32 height = 0;

    if (spec.absoluteSize.isSome()) {
      auto absoluteSize = spec.absoluteSize.unwrap();
      width = absoluteSize.x;
      height = absoluteSize.y;
    } else {
      auto relativeScale = spec.relativeScale.unwrap();
      width = static_cast<u32>(swapchainSize.x * relativeScale.x);
      height = static_cast<u32>(swapchainSize.y * relativeScale.y);
    }


    if (width < 1) {
      width = 1;
    }
    if (height < 1) {
      height = 1;
    }

    ren::ImageBuilder b(name);

    // Set usage flags based on initial access type
    switch (initialAccess) {
      case GraphAccess::RenderTarget:
        b.setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        break;
      case GraphAccess::DepthTarget:
        b.setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        b.setViewAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT);
        break;
      default:
        b.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
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

    if (this->imguiTextureID != VK_NULL_HANDLE) {
      // If we had an ImGui texture ID, we need to remove it, as the image is changing.
      ImGui_ImplVulkan_RemoveTexture(this->imguiTextureID);
      this->imguiTextureID = VK_NULL_HANDLE;
    }



    image = b.build();
  }

  void ImageResource::inspect() const {
    ImGui::Text("Vulkan Image: %p, View: %p", (void *)image->getImage(), (void *)image->getImageView());
    ImGui::Text("Format: %u", spec.format);

    spec.relativeScale.ifSome([](auto scale) {
      ImGui::Text("Swapchain-relative Scale: (%.2f, %.2f)", scale.x, scale.y);
    });

    spec.absoluteSize.ifSome([&](auto size) {
      ImGui::Text("Absolute Size: %u x %u", size.x, size.y);
    });

    if (image) {
      ImGui::Text("Allocated size: %u x %u", image->getWidth(), image->getHeight());
    } else {
      ImGui::TextDisabled("... not yet allocated!");
    }
    if (imguiTextureID == VK_NULL_HANDLE) {
      imguiTextureID = ImGui_ImplVulkan_AddTexture(sampler.getHandle(), image->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    ImGui::Text("ImGui Texture ID: %p", (void *)imguiTextureID);

    // Calculate the width to fit the remaining container space
    float containerWidth = ImGui::GetContentRegionAvail().x;
    float aspectRatio = (float)image->getHeight() / (float)image->getWidth();
    float imageWidth = std::min((u32)containerWidth, image->getWidth());
    float imageHeight = imageWidth * aspectRatio;

    ImGui::Image((ImTextureID)imguiTextureID, ImVec2(imageWidth, imageHeight));

    ImGui::Separator();


    // ImGui::Image((ImTextureID)imguiTextureID,
    //              ImVec2(256, 256 * ((float)image->getHeight() / (float)image->getWidth())));
  }

  void ImageResource::emitBarrier(GraphRunContext &ctx, GraphAccess fromAccess, GraphAccess toAccess) {
    // if (fromAccess == toAccess) {
    //   return;  // No barrier needed
    // }

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
    barrier.subresourceRange.aspectMask = getAspectMask(this->initialAccess);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vkCmdPipelineBarrier(ctx.encoder.buf(), fromInfo.stage, toInfo.stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }

}  // namespace ren
