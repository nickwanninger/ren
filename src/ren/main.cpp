#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>

int main(int argc, char *argv[]) {
  REN_PROFILE_BEGIN_SESSION("Engine Run", "engine_run_profile.json");
  ren::Application app("ren", {2560, 1440});
  app.run();


  REN_PROFILE_END_SESSION();
  return 0;
}
