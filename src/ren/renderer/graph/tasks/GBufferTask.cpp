#include "GBufferTask.h"
#include <ren/renderer/RenderWorld.h>
#include <ren/core/Application.h>
#include <ren/assets/Material.h>
#include "ren/renderer/graph/RunContext.h"

#include <algorithm>
#include <cstring>

#include <imgui/imgui.h>

namespace ren {

  namespace {
    // Matches ObjectData in ren/core.slang.
    struct ObjectData {
      glm::mat4 modelMatrix;
      glm::mat4 normalMatrix;
      u32 objectId;
      u32 _padding[3];
    };
    static_assert(sizeof(ObjectData) == 144);

    // Matches DrawRecord in ren/core.slang.
    struct DrawRecord {
      u32 objectIndex;
      u32 materialIndex;
    };
    static_assert(sizeof(DrawRecord) == 8);

    // The base of each batch's material array. 16 bytes is enough for all
    // natural-layout scalar and vector types.
    constexpr u32 kMaterialBaseAlignment = 16;

    constexpr u32 alignUp(u32 value, u32 alignment) { return (value + alignment - 1) / alignment * alignment; }
  }  // namespace


  GBufferTask::GBufferTask(ren::RenderGraph &G)
      : ren::RenderPassTask(G) {
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
    stats = {};

    // The viewport sets the frame globals (view and projection) before the
    // graph runs, so this task does not read the camera.
    ren::RenderWorld world;
    world.extractFromECS(ren::world());

    // Collect the draws. A material without a schema has no MaterialRoot, so
    // this pass cannot draw it.
    sortedDraws.clear();
    for (u32 i = 0; i < world.renderables.size(); ++i) {
      const auto &r = world.renderables[i];
      if (!r.mesh || !r.material || !r.material->getSchema()) {
        stats.skipped++;
        continue;
      }
      sortedDraws.push_back({r.material->getPSO().hash(), r.material.get(), i});
    }
    if (sortedDraws.empty()) {
      return;
    }

    // Sort by pipeline, then by material. Each pipeline then is one
    // contiguous batch, and each material is one contiguous range in it.
    std::sort(sortedDraws.begin(), sortedDraws.end(), [](const SortedDraw &a, const SortedDraw &b) {
      if (a.psoHash != b.psoHash) return a.psoHash < b.psoHash;
      return a.material < b.material;
    });

    const auto drawCount = static_cast<u32>(sortedDraws.size());
    auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();

    auto &arena = ctx.encoder.getArena();
    auto *objects = arena.push<StorageBuffer<ObjectData>>(drawCount);
    auto *draws = arena.push<StorageBuffer<DrawRecord>>(drawCount);
    auto *commands = arena.push<IndirectBuffer<VkDrawIndexedIndirectCommand>>(drawCount);

    // Fill the per-draw buffers, split the draws into batches, and pack the
    // bytes of each unique material. The draws of one batch index into the
    // material array of that batch.
    batches.clear();
    materialBytes.clear();
    u32 materialIndex = 0;
    u32 materialStride = 0;
    for (u32 d = 0; d < drawCount; ++d) {
      const auto &draw = sortedDraws[d];
      const bool newBatch = batches.empty() || draw.psoHash != sortedDraws[d - 1].psoHash;
      const bool newMaterial = newBatch || draw.material != sortedDraws[d - 1].material;

      if (newBatch) {
        const auto *schema = draw.material->getSchema();
        materialStride = alignUp(schema->byteSize, std::max(schema->alignment, 1u));
        const u32 base = alignUp(static_cast<u32>(materialBytes.size()), kMaterialBaseAlignment);
        materialBytes.resize(base);
        batches.push_back({.material = draw.material, .firstDraw = d, .drawCount = 0, .materialByteOffset = base});
        materialIndex = 0;
      } else if (newMaterial) {
        materialIndex++;
      }

      auto &batch = batches.back();
      batch.drawCount++;

      if (newMaterial) {
        // All materials of a batch use the same program, so they share the
        // schema and the stride.
        const auto &data = draw.material->getCpuMaterialData();
        const size_t offset = materialBytes.size();
        materialBytes.resize(offset + materialStride, 0);
        std::memcpy(materialBytes.data() + offset, data.data(), data.size());
        stats.materials++;
      }

      const auto &r = world.renderables[draw.renderable];
      const auto &entry = megaMesh.getEntry(r.mesh->megaHandle);
      objects->hostData()[d] = ObjectData{
          .modelMatrix = r.transform,
          .normalMatrix = glm::transpose(glm::inverse(r.transform)),
          .objectId = draw.renderable,
      };
      draws->hostData()[d] = DrawRecord{.objectIndex = d, .materialIndex = materialIndex};
      commands->hostData()[d] = VkDrawIndexedIndirectCommand{
          .indexCount = entry.indexCount,
          .instanceCount = 1,
          .firstIndex = entry.indexOffset,
          .vertexOffset = static_cast<i32>(entry.vertexOffset),
          .firstInstance = 0,
      };
    }

    auto *materials = arena.push<StorageBuffer<u8>>(materialBytes.size());
    materials->copyFromHost(materialBytes);

    // Bind the MegaMesh buffer for rendering geometry.
    megaMesh.bind(ctx.encoder.getEncoder());

    for (const auto &batch : batches) {
      const auto *schema = batch.material->getSchema();
      auto shader = ctx.encoder.bindGraphics(batch.material->getPSO());
      auto root = shader.root(schema->pushConstantName);
      // SV_DrawIndex starts at zero for each indirect draw, so the draw
      // records start at the first draw of the batch.
      root.set("draws", draws->devicePointer<DrawRecord>(batch.firstDraw));
      root.set("objects", objects->devicePointer<ObjectData>());
      root.set("materials", materials->devicePointer<u8>(batch.materialByteOffset));
      shader.drawIndexedIndirect(*commands, batch.drawCount, batch.firstDraw * sizeof(VkDrawIndexedIndirectCommand));
    }

    stats.draws = drawCount;
    stats.batches = static_cast<u32>(batches.size());
  }


  void GBufferTask::inspect(void) {
    ImGui::Text("Draws: %u  Batches: %u  Materials: %u  Skipped: %u", stats.draws, stats.batches, stats.materials, stats.skipped);
    graph().getResource(out.albedo)->inspect();
    graph().getResource(out.normal)->inspect();
    graph().getResource(out.metallicRoughness)->inspect();
    graph().getResource(out.depth)->inspect();
  }
}  // namespace ren
