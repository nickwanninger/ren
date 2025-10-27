#include <ren/core/ComponentRegistration.h>

namespace ren {

  std::vector<ComponentRegistrationInfo> &getRegisteredComponents() {
    static std::vector<ComponentRegistrationInfo> registeredComponents;
    return registeredComponents;
  }


  namespace internal {
    bool doRegisterComponent(const ComponentRegistrationInfo &info) {
      auto &cs = ren::getRegisteredComponents();


      // if (info.luaName) {
      //   printf("typedef struct %s { uint8_t __pad[%llu]; } __attribute__((aligned(%llu))) %s;\n",
      //          info.luaName, info.typeSize, info.typeAlignment, info.luaName);
      // }

      cs.push_back(info);
      return true;
    }
  }  // namespace internal
}  // namespace ren