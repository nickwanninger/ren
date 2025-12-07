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

    if (defaultTexture == nullptr)
      defaultTexture = Texture::createSinglePixel("default-white", 255, 255, 255, 255);

    if (defaultNormalTexture == nullptr)
      defaultNormalTexture = Texture::createSinglePixel("default-normal", 127, 127, 255, 255);


    this->baseColorTexture = defaultTexture;
    this->metallicRoughnessTexture = defaultTexture;
    this->emissiveTexture = defaultTexture;
    this->normalTexture = defaultNormalTexture;

    if (PBRMaterial::pso.program == nullptr) {
      // If the PSO is not initialized, create it.
      PBRMaterial::pso.program = makeRef<ShaderProgram>("shaders/pbr");

      PBRMaterial::pso.blendMode = ren::BlendMode::Alpha;
      // PBRMaterial::pso.fillMode = ren::FillMode::Wireframe;

      PBRMaterial::pso.cullMode = ren::CullMode::Back;
    }
  }

  ren::PipelineStateObject &PBRMaterial::getPSO() { return PBRMaterial::pso; }

  bool PBRMaterial::bind(Renderer &R) {
    // Bind the PBR Material's pipeline state object
    R.bind(PBRMaterial::pso);


    // we should also bind the textures to the right spot according to shaders/pbr
    this->materialPropsBuffer.update(this->props);

    // set 1 is the PBR material set for the fragment shader.
    auto binder = R.startBinding(1);

    binder.bind("material", this->materialPropsBuffer);
    binder.bind("baseColorTexture", *this->baseColorTexture);
    binder.bind("metallicRoughnessTexture", *this->metallicRoughnessTexture);
    binder.bind("emissiveTexture", *this->emissiveTexture);
    binder.bind("normalTexture", *this->normalTexture);


    // std::vector<ref<Texture>> textures = {this->baseColorTexture, this->metallicRoughnessTexture,
    //                                       this->emissiveTexture, this->normalTexture};
    // binder.bind("textures", std::span{textures});
    binder.apply();

    return true;
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