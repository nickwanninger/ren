#pragma once

#include <ren/assets/Material.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/Buffer.h>
// This file implements our PBR material workflow.

namespace ren {

  struct PBRMaterialProperties {
    glm::vec4 baseColorFactor = glm::vec4(1.0f); // (R, G, B, A)
    glm::vec4 emissive = glm::vec4(0.0f); // (R, G, B, A) (not sure about A)

    float metallicFactor = 0.0f; // Metallic factor (0.0 to 1.0)
    float roughnessFactor = 1.0f; // Roughness factor (0.0 to 1.0)
  };


  class PBRMaterial : public Material {
   public:
    PBRMaterial();
    ~PBRMaterial() override = default;

    // Bind the material to the renderer.
    bool bind(Renderer &R) override;

    // Render the material to the imgui-based inspector.
    void inspect(void) override;

    // PBR materials are compatible with deferred rendering.
    // in fact, they are really the only material that is.
    inline bool isDeferred(void) const override { return true; }


    PBRMaterialProperties props;

    // GLTF Compatible texture set for pbr metalic roughness model.
    ref<Texture> baseColorTexture;
    ref<Texture> metallicRoughnessTexture;
    ref<Texture> normalTexture;


    UniformBufferSet<PBRMaterialProperties> materialPropsBuffer;

    ren::PipelineStateObject &getPSO();

   private:
    // All PBR materials share the same pipeline state object.
    static ren::PipelineStateObject pso;
  };
}  // namespace ren