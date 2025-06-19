#include <iostream>
#include <vector>

#include <ren/Engine.h>

int main(int argc, char *argv[]) {
  ren::Engine engine("ren", {640 / 2, 480 / 2});

  engine.run();
  return 0;
}
