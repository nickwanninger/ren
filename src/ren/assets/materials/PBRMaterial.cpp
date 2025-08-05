#include <ren/assets/materials/PBRMaterial.h>
#include <imgui/imgui.h>

namespace ren {


  // Static member initialization for the PBRMaterial's pipeline state object.
  ren::PipelineStateObject PBRMaterial::pso;

  PBRMaterial::PBRMaterial() {
    // Set the name of the material.
    this->setName("opaque-pbr-static");

    if (PBRMaterial::pso.program == nullptr) {
      // If the PSO is not initialized, create it.
      PBRMaterial::pso.program = makeRef<ShaderProgram>("shaders/pbr");
    }
  }

  bool PBRMaterial::bind(Renderer &R) {
    REN_PROFILE_SCOPE("PBR_Static");
    // Bind the PBR Material's pipeline state object
    R.bind(PBRMaterial::pso);


    return true;
  }

  void PBRMaterial::inspect(void) {
    // Render the material to the imgui-based inspector.
    ImGui::Text("PBR Material");
    ImGui::Separator();

    // pso.renderInspector();
    // Add any PBR-specific properties here, such as textures, metallic, roughness, etc.

    ImGui::ColorEdit3("Albedo Color", &albedoColor[0]);
    ImGui::ColorEdit3("Emissive Color", &emissiveColor[0]);
    ImGui::ColorEdit3("Specular Color", &specularColor[0]);
  }


}