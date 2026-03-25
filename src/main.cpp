#include "engine/engine.hpp"

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  engine::Engine engine;
  engine.run();

  return 0;
}
