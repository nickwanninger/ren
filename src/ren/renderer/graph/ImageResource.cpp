#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/Renderer.h>

namespace ren {
  namespace {
    struct BarrierInfo {
      VkPipelineStageFlags stage;
      VkAccessFlags access;
      VkImageLayout layout;
    };


    // The pipeline stage, access, and layout of each graph access. ShaderRead
    // is for reads whose stage is not known (e.g. ImGui), so it waits on all
    // commands.
    BarrierInfo barrierInfo(GraphAccess access) {
      switch (access) {
        case GraphAccess::RenderTarget:
          return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        case GraphAccess::DepthTarget:
          return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        case GraphAccess::VertexShaderRead:
          return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        case GraphAccess::FragmentShaderRead:
          return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        case GraphAccess::ComputeShaderRead:
          return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        case GraphAccess::ComputeShaderWrite:
          return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
        case GraphAccess::ComputeShaderReadWrite:
          return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
        case GraphAccess::ShaderRead:
          return {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      }
      throw std::runtime_error("Unsupported graph access");
    }
  }  // namespace


  ImageStorage::~ImageStorage() {
    if (image) {
      image->releaseSampledIndex();
    }
  }


  TextureHandle ImageStorage::textureHandle() const { return TextureHandle::pack(image->sampledIndex(), sampler->index()); }


  void ImageStorage::transition(RecordContext &ctx, const std::vector<GraphAccess> &from, GraphAccess to) const {
    const auto destination = barrierInfo(to);

    // Combine the stages of all earlier readers, so that a write waits for
    // each of them. An empty from leaves the layout UNDEFINED.
    VkPipelineStageFlags stages = 0;
    VkAccessFlags accesses = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (auto access : from) {
      auto source = barrierInfo(access);
      stages |= source.stage;
      accesses |= source.access;
      layout = source.layout;
    }

    // A combined depth/stencil format needs both aspects in a layout transition.
    VkImageAspectFlags aspects = VK_IMAGE_ASPECT_COLOR_BIT;
    if (depth) {
      aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
      if (hasStencilComponent(image->getFormat())) {
        aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
      }
    }

    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = accesses,
        .dstAccessMask = destination.access,
        .oldLayout = layout,
        .newLayout = destination.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->getImage(),
        .subresourceRange = {.aspectMask = aspects, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };

    // With no earlier access, there is nothing to wait for.
    auto sourceStages = stages ? stages : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    vkCmdPipelineBarrier(ctx.encoder.buf(), sourceStages, destination.stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }


  ImageResource::ImageResource(const GraphImageSpec &spec, GraphAccess access)
      : spec(spec)
      , initialAccess(access) {
    temporal = spec.temporal;
    sampler = Renderer::get().getSamplerCache().get({});
  }


  bool ImageResource::compatible(const GraphStorage &storage, const CapturedView *view) const {
    const auto &image = dynamic_cast<const ImageStorage &>(storage).image;
    auto size = spec.size.resolve(view);
    return image->getWidth() == size.x && image->getHeight() == size.y && image->getFormat() == spec.format;
  }


  ref<GraphStorage> ImageResource::allocate(const CapturedView *view) const {
    auto size = spec.size.resolve(view);
    auto result = make<ImageStorage>();
    result->depth = initialAccess == GraphAccess::DepthTarget;
    result->sampler = sampler;

    // Every graph image can be sampled, copied, and used as an attachment. Only
    // images that a compute shader writes are storage images.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    usage |= result->depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (initialAccess == GraphAccess::ComputeShaderWrite || initialAccess == GraphAccess::ComputeShaderReadWrite) {
      usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    result->image = ImageBuilder(name)
                        .setWidth(size.x)
                        .setHeight(size.y)
                        .setFormat(spec.format)
                        .setUsage(usage)
                        .setViewAspectMask(result->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT)
                        .setInitialLayout(VK_IMAGE_LAYOUT_UNDEFINED)
                        .build();
    return result;
  }
}  // namespace ren
