#include <iostream>
#include <vector>

#include <ren/Engine.h>
#include <ren/core/Instrumentation.h>
int main(int argc, char *argv[]) {
  REN_PROFILE_BEGIN_SESSION("Engine Run", "engine_run_profile.json");
  ren::Engine engine("ren", {1280, 720});

  // REN_PROFILE_OUTPUT(true);
  engine.run();
  REN_PROFILE_END_SESSION();
  return 0;
}
