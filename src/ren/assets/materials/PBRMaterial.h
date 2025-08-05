#pragma once

#include <ren/assets/Material.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
// This file implements our PBR material workflow.

namespace ren {

  struct PBRMaterialProperties {
    // // Base color texture.
    // ref<Texture> baseColorTexture;
    // // Metallic texture.
    // ref<Texture> metallicTexture;
    // // Roughness texture.
    // ref<Texture> roughnessTexture;
    // // Normal map texture.
    // ref<Texture> normalMapTexture;

    // Base color factor (RGBA).
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    // Metallic factor.
    float metallicFactor = 1.0f;
    // Roughness factor.
    float roughnessFactor = 1.0f;
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



    glm::vec3 albedoColor = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 emissiveColor = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 specularColor = glm::vec3(0.0f, 0.0f, 0.0f);


   private:
    // All PBR materials share the same pipeline state object.
    static ren::PipelineStateObject pso;
  };
}  // namespace ren