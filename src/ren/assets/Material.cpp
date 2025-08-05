#include <ren/assets/Material.h>
#include <imgui/imgui.h>

namespace ren {



  void Material::inspect(void) {
    ImGui::Text("Material '%s', ID: %llu", getName().c_str(), (u64)getAssetID());
  }



}  // namespace ren