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
    PipelineCache();
    ~PipelineCache();

    // No copy, no move
    PipelineCache(const PipelineCache &) = delete;
    PipelineCache &operator=(const PipelineCache &) = delete;
    PipelineCache(PipelineCache &&) = delete;
    PipelineCache &operator=(PipelineCache &&) = delete;

    ref<CachedPipeline> get(ren::RenderPass &renderPass, const PipelineStateObject &pso);

    void save(std::string_view filename) const;
    void load(std::string_view filename);

    size_t size(void) const { return pipelines.size(); }

   private:
    VkPipelineCache vkCache = VK_NULL_HANDLE;
    std::unordered_map<u64, ref<CachedPipeline>> pipelines;
  };
}  // namespace ren