#include <ren/core/ComponentRegistration.h>

namespace ren {

  std::vector<ComponentRegistrationInfo> &getRegisteredComponents() {
    static std::vector<ComponentRegistrationInfo> registeredComponents;
    return registeredComponents;
  }


  namespace internal {
    bool doRegisterComponent(const ComponentRegistrationInfo &info) {
      auto &cs = ren::getRegisteredComponents();
      cs.push_back(info);
      return true;
    }
  }  // namespace internal
}  // namespace ren