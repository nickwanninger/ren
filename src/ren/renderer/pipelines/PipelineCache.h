#pragma once

#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/RenderPass.h>
#include <ren/types.h>

namespace ren {

  class CachedPipeline : public ren::VulkanPipeline {
   public:
    inline CachedPipeline(VkPipeline pipeline, VkPipelineLayout pipelineLayout,
                          VkDescriptorSetLayout descriptorSetLayout)
        : ren::VulkanPipeline()
        , descriptorSetLayout(descriptorSetLayout) {
      this->pipeline = pipeline;
      this->pipelineLayout = pipelineLayout;
    }

    const auto &getDescriptorSetLayout(void) const { return descriptorSetLayout; }

   private:
    VkDescriptorSetLayout descriptorSetLayout;
  };

  class PipelineCache {
   public:
    ref<CachedPipeline> get(ren::RenderPass &renderPass, const PipelineStateObject &pso,
                            VkDescriptorSetLayout descriptorSetLayout);

    size_t size(void) const { return pipelines.size(); }

   private:
    std::unordered_map<u64, ref<CachedPipeline>> pipelines;
  };
}  // namespace ren