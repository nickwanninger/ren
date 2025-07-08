#pragma once

#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/RenderPass.h>
#include <ren/assets/Vertex.h>
#include <ren/types.h>

namespace ren {

  class CachedPipeline : public ren::VulkanPipeline {
   public:
    inline CachedPipeline(VkPipeline pipeline, PipelineStateObject pso)
        : ren::VulkanPipeline()
        , pso(pso) {
      this->pipeline = pipeline;
    }

    const auto &getDescriptorSetLayouts(void) const { return pso.program->getDescriptorSetLayouts(); }

    auto getLayout(void) const { return pso.program->getPipelineLayout(); }

    const auto &getPSO(void) const { return pso; }

   private:
    PipelineStateObject pso;
  };

  class PipelineCache {
   public:
    ref<CachedPipeline> get(ren::RenderPass &renderPass, const PipelineStateObject &pso);

    size_t size(void) const { return pipelines.size(); }

   private:
    std::unordered_map<u64, ref<CachedPipeline>> pipelines;
  };
}  // namespace ren