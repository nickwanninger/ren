#include <iostream>
#include <vector>

#include <ren/Engine.h>

int main(int argc, char *argv[]) {
  ren::Engine engine("ren", {1920, 1080});

  engine.run();
  return 0;
}
