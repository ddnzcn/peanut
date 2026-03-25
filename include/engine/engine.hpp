#ifndef ENGINE_ENGINE_HPP
#define ENGINE_ENGINE_HPP

namespace engine {

class Engine {
public:
  Engine();

  void run();

private:
  void init();
  void tick();
  void shutdown();

  bool running_;
  unsigned int frame_count_;
};

}  // namespace engine

#endif
