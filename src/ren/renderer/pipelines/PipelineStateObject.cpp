#include <ren/renderer/pipelines/PipelineStateObject.h>

#include <imgui/imgui.h>
#include <ren/misc/hash.h>


namespace ren {




  template <typename T>
  static void enumSelector(const char* label, T& value) {
    auto enumMap = getEnumMap((T)value);

    if (ImGui::BeginCombo(label, enumMap[value].c_str())) {
      for (const auto& [enumValue, enumName] : enumMap) {
        bool isSelected = (enumValue == value);
        if (ImGui::Selectable(enumName.c_str(), isSelected)) { value = enumValue; }
        if (isSelected) { ImGui::SetItemDefaultFocus(); }
      }
      ImGui::EndCombo();
    }
  }


  u64 PipelineStateObject::hash(void) const  {
    // Generate a hash for this PSO based on its properties.
    u64 state = 0;
    ren::hash(state, (u64)program->getUUID());
    ren::hash(state, topology);
    ren::hash(state, depthTest);
    ren::hash(state, depthWrite);
    ren::hash(state, depthClip);
    ren::hash(state, fillMode);
    ren::hash(state, cullMode);
    ren::hash(state, frontCCW);
    ren::hash(state, depthBias);
    ren::hash(state, depthBiasClamp);
    ren::hash(state, depthSlopeFactor);
    ren::hash(state, blendMode);
    return state;
  }

  void PipelineStateObject::renderInspector() {
    // render the imgui inspector for this pso.

    // TODO: shader selector!

    enumSelector("Topology", topology);
    enumSelector("Fill Mode", fillMode);
    enumSelector("Cull Mode", cullMode);
    enumSelector("Blend Mode", blendMode);
    ImGui::Checkbox("Front CCW", &frontCCW);

    if (ImGui::CollapsingHeader("Depth Settings")) {
      ImGui::Checkbox("Depth Test", &depthTest);
      ImGui::Checkbox("Depth Write", &depthWrite);
      ImGui::Checkbox("Depth Clip", &depthClip);
      ImGui::SliderFloat("Depth Bias", &depthBias, -1.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Depth Bias Clamp", &depthBiasClamp, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Depth Slope Factor", &depthSlopeFactor, 0.0f, 1.0f, "%.2f");
    }
  }



  // -------------------------------------------------------------------------

  void to_json(json& j, const PipelineStateObject& pso) {
    // j["vertexShader"] = pso.vertexShader->getUUID();
    // j["fragmentShader"] = pso.fragmentShader->getUUID();
    j["program"] = *pso.program;
    j["topology"] = pso.topology;
    j["depthTest"] = pso.depthTest;
    j["depthWrite"] = pso.depthWrite;
    j["depthClip"] = pso.depthClip;
    j["fillMode"] = pso.fillMode;
    j["cullMode"] = pso.cullMode;
    j["frontCCW"] = pso.frontCCW;
    j["depthBias"] = pso.depthBias;
    j["depthBiasClamp"] = pso.depthBiasClamp;
    j["depthSlopeFactor"] = pso.depthSlopeFactor;
    j["blendMode"] = pso.blendMode;
  }

  void from_json(const json& j, PipelineStateObject& pso) {
    // skip shaders for now.
    pso.topology = j.at("topology").get<Topology>();
    pso.depthTest = j.at("depthTest").get<bool>();
    pso.depthWrite = j.at("depthWrite").get<bool>();
    pso.depthClip = j.at("depthClip").get<bool>();
    pso.fillMode = j.at("fillMode").get<FillMode>();
    pso.cullMode = j.at("cullMode").get<CullMode>();
    pso.frontCCW = j.at("frontCCW").get<bool>();
    pso.depthBias = j.at("depthBias").get<float>();
    pso.depthBiasClamp = j.at("depthBiasClamp").get<float>();
    pso.depthSlopeFactor = j.at("depthSlopeFactor").get<float>();
    pso.blendMode = j.at("blendMode").get<BlendMode>();
  }

}  // namespace ren