#pragma once

#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/graph/RenderGraph.h>

#include <vector>

namespace ren {

  class Material;
  class RenderWorld;

  // Draws all renderables into the G-buffer. Each material supplies its own
  // PSO (a surface linked with ren/pass/deferred) and its parameter bytes.
  // Draws that share a pipeline go into one batch: one PSO bind and one
  // indirect draw. A draw selects its object and material through
  // draws[SV_DrawIndex] (see MaterialRoot in ren/core.slang).
  class GBufferTask : public ren::RenderPassTask {
   public:
    struct {
      ren::GraphHandle albedo;             // albedo result
      ren::GraphHandle normal;             // normal result
      ren::GraphHandle metallicRoughness;  // metallic-roughness result
      ren::GraphHandle depth;              // depth result
    } out;

    GBufferTask(ren::RenderGraph &G);

    void prepareFrame(PrepareContext &) override;
    void run(ren::GraphRenderPassContext &ctx) const override;
    void inspect(void) override;

   private:
    // One renderable that passed the checks, with its sort key.
    struct SortedDraw {
      u64 psoHash;
      ren::Material *material;
      u32 renderable;
    };

    // A range of sorted draws that use the same pipeline.
    struct Batch {
      // The first material of the batch. It gives the PSO and the schema.
      ren::Material *material;
      u32 firstDraw;
      u32 drawCount;
      // Byte offset of the batch's first material in the material buffer.
      u32 materialByteOffset;
    };

    ref<RenderWorld> preparedWorld;

    // Statistics of the last run, for the inspector.
    struct {
      u32 draws = 0;
      u32 batches = 0;
      u32 materials = 0;
      u32 skipped = 0;
    } stats;
  };


  // TODO: figure out how to parameterize this with different cameras!
  inline auto &addGBuffer(GraphScope &G, ren::GraphHandle &albedo, ren::GraphHandle &normal, ren::GraphHandle &metallicRoughness,
                          ren::GraphHandle &depth) {
    auto &pass = G.addTask<GBufferTask>("gbuffer");

    albedo = pass.out.albedo;
    normal = pass.out.normal;
    metallicRoughness = pass.out.metallicRoughness;
    depth = pass.out.depth;

    return pass;
  }

}  // namespace ren
