#ifndef GAME_GAMECONTEXT_HPP
#define GAME_GAMECONTEXT_HPP

#include "engine/scene/SceneTree.hpp"

#include <cstdint>

namespace engine
{
class BehaviorRegistry;
class SignalBus;
} // namespace engine

namespace game
{

struct GameContext
{
  int cameraX = 0;
  int cameraY = 0;
  int cameraSpeed = 2;
  float renderScale = 4.0f;

  // Screen viewport size in pixels, set by the engine each frame.
  int viewW = 640;
  int viewH = 512;

  // World bounds in pixels (from the main tilemap). 0 = unbounded (no clamp).
  int worldWidthPx = 0;
  int worldHeightPx = 0;

  uint8_t clearR = 8;
  uint8_t clearG = 16;
  uint8_t clearB = 32;

  bool debugTileGrid = false;
  bool debugChunkGrid = false;

  engine::SceneTree *sceneTree = nullptr;
  engine::BehaviorRegistry *behaviorRegistry = nullptr;
  engine::SignalBus *signalBus = nullptr;

  uint16_t padButtons = 0;

  // Request a scene swap by writing the path (must outlive the current frame:
  // a string literal or persistent buffer). The engine consumes and clears
  // this between frames.
  const char *requestedScene = nullptr;

  // Optional: request an atlas swap. If both paths are non-null the engine
  // swaps the atlas BEFORE swapping the scene, so the new scene resolves
  // against the new sprite IDs. Leave null to keep the current atlas.
  const char *requestedAtlasMeta = nullptr;
  const char *requestedAtlasData = nullptr;
  uint16_t requestedAtlasPage = 0;
};

} // namespace game

#endif
