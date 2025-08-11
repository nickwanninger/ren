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
      defaultNormalTexture = Texture::createSinglePixel("default-normal", 128, 128, 255, 255);


    this->baseColorTexture = defaultTexture;
    this->metallicRoughnessTexture = defaultTexture;
    this->normalTexture = defaultNormalTexture;

    if (PBRMaterial::pso.program == nullptr) {
      // If the PSO is not initialized, create it.
      PBRMaterial::pso.program = makeRef<ShaderProgram>("shaders/pbr");
    }
  }

  bool PBRMaterial::bind(Renderer &R) {
    REN_PROFILE_SCOPE("PBR_Static");
    // Bind the PBR Material's pipeline state object
    R.bind(PBRMaterial::pso);


    // we should also bind the textures to the right spot according to shaders/pbr
    this->materialPropsBuffer.update(this->props);

    // set 1 is the PBR material set for the fragment shader.
    auto binder = R.startBinding(0);

    binder.bind("material", this->materialPropsBuffer);
    binder.bind("baseColorTexture", *this->baseColorTexture);
    binder.bind("metallicRoughnessTexture", *this->metallicRoughnessTexture);
    binder.bind("normalTexture", *this->normalTexture);
    binder.apply();

    return true;
  }

  void PBRMaterial::inspect(void) {
    // Render the material to the imgui-based inspector.
    ImGui::Text("PBR Material");
    ImGui::Separator();

    // pso.renderInspector();
    // Add any PBR-specific properties here, such as textures, metallic, roughness, etc.

    ImGui::ColorEdit3("Albedo Color", &props.baseColorFactor[0]);
    ImGui::ColorEdit3("Emissive Color", &props.emissive[0]);
  }


}  // namespace ren