#ifndef GAME_GAME_HPP
#define GAME_GAME_HPP

#include "game/GameContext.hpp"

namespace engine
{
class SceneTree;
class PscnFile;
} // namespace engine

namespace game
{

struct GameConfig
{
  const char *atlasMetaPath = "atlas.meta.bin";
  const char *atlasDataPath = "atlas.bin";
  const char *scenePath = "scene.pscn.bin";
  uint16_t atlasPageIndex = 0;
};

GameConfig Configure();

// One-time game setup at engine startup. Behaviors register here via
// ctx.behaviorRegistry (set by the engine before Init runs). Should NOT
// query the scene — the scene is not loaded yet at this point.
void Init(GameContext &ctx);

// Called after every scene build (initial load + each swap). The scene tree
// is fully populated and ready to query, but behaviors have not yet bound.
// Use this for per-scene setup: deriving world bounds, picking a render
// scale, etc.
void OnSceneLoaded(GameContext &ctx);

void Update(GameContext &ctx);

void Shutdown(GameContext &ctx);

} // namespace game

#endif
