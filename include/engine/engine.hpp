#ifndef ENGINE_ENGINE_HPP
#define ENGINE_ENGINE_HPP

#include "engine/scene/PscnLoader.hpp"
#include "engine/scene/SceneTree.hpp"
#include "engine/scene/BehaviorRegistry.hpp"
#include "engine/scene/SceneRenderer.hpp"
#include "atlas2d/AtlasPack.hpp"
#include "game/Game.hpp"

extern "C"
{
#include <gsKit.h>
}

namespace engine
{

class Engine
{
public:
  Engine();

  void run();

private:
  void initHardware();
  bool loadAssets(const game::GameConfig &config);
  bool swapAtlas(const char *metaPath, const char *dataPath, uint16_t pageIndex);
  bool swapScene(const char *scenePath);
  void tick();
  void shutdown();

  bool running_;
  unsigned int frame_count_;

  PscnFile m_pscnFile;
  SceneTree m_sceneTree;
  BehaviorRegistry m_behaviorRegistry;
  atlas2d::AtlasPack m_atlasPack;
  game::GameContext m_gameCtx;

  // Edge-detect bookkeeping lives on the engine, not GameContext.
  uint16_t m_prevPadButtons = 0;
  uint32_t m_padPressedHash = 0;
  uint32_t m_padReleasedHash = 0;
};

} // namespace engine

#endif
