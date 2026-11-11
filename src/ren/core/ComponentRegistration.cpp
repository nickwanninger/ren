#include <ren/core/ComponentRegistration.h>

namespace ren {

  std::deque<ComponentRegistrationInfo> &getRegisteredComponents() {
    static std::deque<ComponentRegistrationInfo> registeredComponents;
    return registeredComponents;
  }


  void bindRegisteredComponents(flecs::world &world) {
    for (const auto &info : getRegisteredComponents()) {
      info.bindToWorld(world, info);
    }
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