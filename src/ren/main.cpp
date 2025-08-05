#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>

#include <ren/core/Components.h>
#include <flecs.h>

#include <type_traits>



int main(int argc, char *argv[]) {
 REN_PROFILE_BEGIN_SESSION("Engine Run", "engine_run_profile.json");
  ren::Application app("ren", {1280, 720});

  app.run();

  REN_PROFILE_END_SESSION();
  return 0;
}
