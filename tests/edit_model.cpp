#include <ren/core/Components.h>
#include <ren/core/ComponentRegistration.h>
#include <ren/obj/Types.h>
#include <iostream>
#include <string_view>

using namespace ren;
namespace {
  void verify(bool value, const char *message) {
    if (!value) {
      throw std::runtime_error(message);
    }
  }


  const ComponentRegistrationInfo &findRegistration(std::string_view name) {
    for (const auto &info : getRegisteredComponents()) {
      if (info.type != nullptr && info.type->name == name) {
        return info;
      }
    }
    throw std::runtime_error("Component is not registered with a schema: " + std::string(name));
  }



  //=========================
  // Component schemas
  //=========================

  void testRegistrationUsesObjTypes() {
    verify(findRegistration("Name").type == &obj::getType<comp::Name>(), "Name registration has another type");
    verify(findRegistration("Transform").type == &obj::getType<comp::Transform>(), "Transform registration has another type");

    // Components without describe() still register for flecs.
    for (const auto &info : getRegisteredComponents()) {
      if (std::string_view(info.typeName).find("PointLightComponent") != std::string_view::npos) {
        verify(info.type == nullptr, "PointLightComponent has no describe() but has a schema");
        return;
      }
    }
    throw std::runtime_error("PointLightComponent is not registered");
  }


  void testTransformRoundTrip() {
    const auto &type = obj::getType<comp::Transform>();
    verify(type.fields.size() == 3, "Transform must expose translation, rotation and scale only");

    comp::Transform source;
    source.translation = {1.0f, 2.0f, 3.0f};
    source.scale = {4.0f, 5.0f, 6.0f};

    // A default value supplies the field defaults, as a scene load does.
    auto value = obj::readNative(comp::Transform{}).deserialize(obj::readNative(source).serialize(), "Transform");
    auto baked = value.bake<comp::Transform>();
    verify(baked.translation == source.translation, "translation does not round trip");
    verify(baked.rotation == source.rotation, "rotation does not round trip");
    verify(baked.scale == source.scale, "scale does not round trip");
  }
}  // namespace


int main() {
  testRegistrationUsesObjTypes();
  testTransformRoundTrip();
  std::cout << "Edit model tests passed\n";
  return 0;
}
