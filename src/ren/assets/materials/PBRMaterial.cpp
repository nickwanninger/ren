#include <ren/assets/materials/PBRMaterial.h>
#include <imgui/imgui.h>

namespace ren {


  // Static member initialization for the PBRMaterial's pipeline state object.
  ren::PipelineStateObject PBRMaterial::pso;
  static ref<Texture> defaultTexture = nullptr;
  static ref<Texture> defaultNormalTexture = nullptr;

  PBRMaterial::PBRMaterial() {
    // Set the name of the material.
    this->setName("opaque-pbr-static");

    if (defaultTexture == nullptr) {
      defaultTexture = Texture::createSinglePixel("default-white", 255, 255, 255, 255);
    }

    if (defaultNormalTexture == nullptr) {
      defaultNormalTexture = Texture::createSinglePixel("default-normal", 127, 127, 255, 255);
    }


    this->baseColorTexture = defaultTexture;
    this->metallicRoughnessTexture = defaultTexture;
    this->emissiveTexture = defaultTexture;
    this->normalTexture = defaultNormalTexture;

    // PBR restoration is intentionally deferred until its resources are
    // expressed as bindless handles and buffer addresses.
    this->pso.debugName = "PBR Material PSO";
    this->pso.program = make<ShaderProgram>("pbr");
    this->pso.cullMode = ren::CullMode::None;
    this->pso.depthTest = false;
    this->pso.depthWrite = false;
  }

  ren::PipelineStateObject &PBRMaterial::getPSO() { return PBRMaterial::pso; }

  BoundGraphicsEncoder PBRMaterial::bind(RenderPassEncoder &enc) {


    auto genc = enc.bindGraphics(PBRMaterial::pso);


    // TODO:
    // genc.

    return genc;
  }

  void PBRMaterial::inspect(void) {
    // Render the material to the imgui-based inspector.
    ImGui::Text("PBR Material");
    ImGui::Separator();

    pso.renderInspector();
    // Add any PBR-specific properties here, such as textures, metallic, roughness, etc.

    ImGui::ColorEdit4("Albedo Color", &props.baseColorFactor[0]);
    ImGui::ColorEdit4("Emissive Color", &props.emissive[0]);

    ImGui::SliderFloat("Metallic", &props.metallicFactor, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &props.roughnessFactor, 0.0f, 1.0f);

    ImGui::Text("Textures");
    ImGui::Separator();

    ImGui::Text("Base Color Texture");
    ImGui::Image(baseColorTexture->getImGui(), ImVec2(100, 100));

    ImGui::Text("Metallic Roughness Texture");
    ImGui::Image(metallicRoughnessTexture->getImGui(), ImVec2(100, 100));

    ImGui::Text("Normal Texture");
    ImGui::Image(normalTexture->getImGui(), ImVec2(100, 100));
  }


}  // namespace ren
