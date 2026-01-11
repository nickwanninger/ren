#include <ren/assets/Material.h>
#include <imgui/imgui.h>

namespace ren {



  void Material::inspect(void) {
    ImGui::Text("Material '%s'", name.c_str());
  }



}  // namespace ren