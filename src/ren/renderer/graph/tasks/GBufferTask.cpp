#include "GBufferTask.h"
#include <ren/Camera.h>
#include <ren/renderer/RenderWorld.h>
#include <ren/core/Application.h>
#include <ren/core/DebugLines.hpp>
#include <ren/assets/materials/PBRMaterial.h>
#include <ren/renderer/shader/ShaderCache.h>
#include "ren/renderer/graph/RunContext.h"
#include <unordered_map>

namespace ren {

  GBufferTask::GBufferTask(ren::RenderGraph &G)
      : ren::RenderPassTask(G) {
    pso.debugName = "GBuffer";
    pso.program = ren::getShader("demo/plain");
    auto scale = glm::vec2(1.0f);
    // Location 0
    this->out.albedo = addColorAttachment("gbufferAlbedo", {.relativeScale = scale, .format = VK_FORMAT_R16G16B16A16_SFLOAT});
    // Location 1
    this->out.normal = addColorAttachment("gbufferNormal", {.relativeScale = scale, .format = VK_FORMAT_R16G16B16A16_SFLOAT});
    // Location 2
    this->out.metallicRoughness = addColorAttachment("gbufferMetallicRoughness", {.relativeScale = scale, .format = VK_FORMAT_R8G8B8A8_UNORM});
    this->out.depth = addDepthAttachment("gbufferDepth", {.relativeScale = scale, .format = VK_FORMAT_D32_SFLOAT});
  }


  void GBufferTask::run(ren::GraphRenderPassContext &ctx) {
    auto &cam = ren::Camera::get();
    auto viewMatrix = cam.view_matrix();

    // Grab an image for width/height.
    auto image = ctx.graph.getImage(out.depth);
    auto projection = ren::Camera::projectionMatrix(image->getWidth(), image->getHeight());


    auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();
    // Bind the MegaMesh buffer for rendering geometry.
    megaMesh.bind(ctx.encoder.getEncoder());


    // Extract the renderables.
    ren::RenderWorld world;
    world.extractFromECS(ren::world());

    const auto drawCount = world.renderables.size();
    if (drawCount == 0) return;

    struct alignas(16) ObjectData {
      glm::mat4 modelMatrix;
      glm::mat4 normalMatrix;
      u32 objectId;
      u32 padding[3];
    };
    struct MaterialData {
      glm::vec4 color;
      float metallic;
      float roughness;
      float padding[2];
    };
    struct DrawRecord {
      u32 objectIndex;
      u32 materialIndex;
    };

    auto &arena = ctx.encoder.getArena();
    auto *objects = arena.push<StorageBuffer<ObjectData>>(drawCount);
    auto *materials = arena.push<StorageBuffer<MaterialData>>(drawCount);
    auto *draws = arena.push<StorageBuffer<DrawRecord>>(drawCount);
    auto *commands = arena.push<IndirectBuffer<VkDrawIndexedIndirectCommand>>(drawCount);
    std::unordered_map<const ren::Material *, u32> materialIndices;
    materialIndices.reserve(drawCount);
    u32 materialCount = 0;

    for (u32 i = 0; i < drawCount; ++i) {
      const auto &r = world.renderables[i];
      const auto &entry = megaMesh.getEntry(r.mesh->megaHandle);
      objects->hostData()[i] = ObjectData{
          .modelMatrix = r.transform,
          .normalMatrix = glm::transpose(glm::inverse(r.transform)),
          .objectId = i,
      };

      auto [it, inserted] = materialIndices.try_emplace(r.material.get(), materialCount);
      if (inserted) {
        MaterialData data{.color = glm::vec4(1.0f), .metallic = 0.0f, .roughness = 1.0f};
        if (auto *pbr = dynamic_cast<const ren::PBRMaterial *>(r.material.get())) {
          data.color = pbr->props.baseColorFactor;
          data.metallic = pbr->props.metallicFactor;
          data.roughness = pbr->props.roughnessFactor;
        }
        materials->hostData()[materialCount++] = data;
      }
      draws->hostData()[i] = DrawRecord{i, it->second};
      commands->hostData()[i] = VkDrawIndexedIndirectCommand{
          .indexCount = entry.indexCount,
          .instanceCount = 1,
          .firstIndex = entry.indexOffset,
          .vertexOffset = static_cast<i32>(entry.vertexOffset),
          .firstInstance = 0,
      };
    }

    auto shader = ctx.encoder.bindGraphics(pso);
    auto root = shader.root("root");
    root.set("draws", draws->devicePointer<DrawRecord>());
    root.set("objects", objects->devicePointer<ObjectData>());
    root.set("materials", materials->devicePointer<MaterialData>());
    shader.drawIndexedIndirect(*commands, static_cast<u32>(drawCount));
  }


  void GBufferTask::inspect(void) {
    graph().getResource(out.albedo)->inspect();
    graph().getResource(out.normal)->inspect();
    graph().getResource(out.metallicRoughness)->inspect();
    graph().getResource(out.depth)->inspect();
  }
}  // namespace ren
