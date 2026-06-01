#ifndef ENGINE_SCENE_SCENERENDERER_HPP
#define ENGINE_SCENE_SCENERENDERER_HPP

#include "engine/scene/PscnTypes.hpp"
#include "engine/scene/PscnLoader.hpp"
#include "engine/scene/SceneTree.hpp"
#include "atlas2d/AtlasPack.hpp"

extern "C"
{
#include <gsKit.h>
}

namespace engine
{

struct SceneRenderParams
{
  int cameraX = 0;
  int cameraY = 0;
  int viewW = 640;
  int viewH = 512;
  float scale = 1.0f;
  uint32_t timeMs = 0;
};

void RenderScene(GSGLOBAL *gsGlobal,
                 const SceneTree &tree,
                 const PscnFile &file,
                 const atlas2d::AtlasPack &atlas,
                 GSTEXTURE *texture,
                 uint16_t atlasPageIndex,
                 const SceneRenderParams &params);

} // namespace engine

#endif
