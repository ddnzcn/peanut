#include "game/Game.hpp"
#include "game/Behaviors.hpp"

#include "engine/scene/BehaviorRegistry.hpp"

namespace game
{

GameConfig Configure()
{
  GameConfig config;
  config.atlasMetaPath = "atlas.meta.bin";
  config.atlasDataPath = "atlas.bin";
  config.scenePath = "base0.pscn.bin";
  config.atlasPageIndex = 0;
  return config;
}

void Init(GameContext &ctx)
{
  ctx.renderScale = 2.0f;
  ctx.cameraSpeed = 2;
  ctx.clearR = 8;
  ctx.clearG = 16;
  ctx.clearB = 32;

  if (ctx.behaviorRegistry)
  {
    RegisterAllBehaviors(*ctx.behaviorRegistry);
  }
}

void OnSceneLoaded(GameContext &ctx)
{
  if (!ctx.sceneTree)
  {
    return;
  }

  // Reset bounds — next scene may have a different background size, or none.
  ctx.worldWidthPx = 0;
  ctx.worldHeightPx = 0;

  // Reset camera so the centering logic in Update doesn't carry over old
  // values into the new scene.
  ctx.cameraX = 0;
  ctx.cameraY = 0;

  const engine::SceneNode *bg = ctx.sceneTree->FindNodeByScriptId("background");
  if (bg)
  {
    const engine::PscnTileMapExt *tm =
        ctx.sceneTree->GetExtensionAs<engine::PscnTileMapExt>(*bg);
    if (tm)
    {
      ctx.worldWidthPx = static_cast<int>(tm->mapWidthTiles) * static_cast<int>(tm->tileWidth);
      ctx.worldHeightPx = static_cast<int>(tm->mapHeightTiles) * static_cast<int>(tm->tileHeight);
    }
  }
}

void Update(GameContext &ctx)
{
  if (!ctx.sceneTree)
  {
    return;
  }

  // Camera follows the player node, centered on screen. Runs every frame
  // (global game logic), not a behavior, so it works without a 'camera' node
  // in the scene.
  const engine::SceneNode *player = ctx.sceneTree->FindNodeByScriptId("player");
  if (player)
  {
    const float invScale = (ctx.renderScale > 0.0f) ? (1.0f / ctx.renderScale) : 1.0f;
    const int halfViewWorldW = static_cast<int>(static_cast<float>(ctx.viewW) * invScale * 0.5f);
    const int halfViewWorldH = static_cast<int>(static_cast<float>(ctx.viewH) * invScale * 0.5f);

    ctx.cameraX = (player->world.worldX >> 16) - halfViewWorldW;
    ctx.cameraY = (player->world.worldY >> 16) - halfViewWorldH;

    if (ctx.worldWidthPx > 0)
    {
      const int viewWorldW = static_cast<int>(static_cast<float>(ctx.viewW) * invScale);
      const int maxX = (ctx.worldWidthPx > viewWorldW) ? (ctx.worldWidthPx - viewWorldW) : 0;
      if (ctx.cameraX < 0) ctx.cameraX = 0;
      if (ctx.cameraX > maxX) ctx.cameraX = maxX;
    }
    if (ctx.worldHeightPx > 0)
    {
      const int viewWorldH = static_cast<int>(static_cast<float>(ctx.viewH) * invScale);
      const int maxY = (ctx.worldHeightPx > viewWorldH) ? (ctx.worldHeightPx - viewWorldH) : 0;
      if (ctx.cameraY < 0) ctx.cameraY = 0;
      if (ctx.cameraY > maxY) ctx.cameraY = maxY;
    }
  }
}

void Shutdown(GameContext &ctx)
{
  (void)ctx;
}

} // namespace game
