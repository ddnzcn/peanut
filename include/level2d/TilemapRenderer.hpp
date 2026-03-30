#ifndef LEVEL2D_TILEMAPRENDERER_HPP
#define LEVEL2D_TILEMAPRENDERER_HPP

#include "atlas2d/AtlasPack.hpp"
#include "level2d/TilemapRuntime.hpp"

extern "C"
{
#include <gsKit.h>
}

namespace level2d
{

  struct TilemapRenderParams
  {
    int cameraX = 0;
    int cameraY = 0;
    float originX = 0.0f;
    float originY = 0.0f;
    float scale = 1.0f;
  };

  bool DrawChunk(GSGLOBAL *gsGlobal,
                 const Tilemap &tilemap,
                 const atlas2d::AtlasPack &atlasPack,
                 uint16_t atlasPageIndex,
                 GSTEXTURE *texture,
                 const LayerDef &layer,
                 const TileChunkView &chunkView,
                 const TilemapRenderParams &params);

  bool DrawVisibleLayer(GSGLOBAL *gsGlobal,
                        const Tilemap &tilemap,
                        const atlas2d::AtlasPack &atlasPack,
                        uint32_t layerIndex,
                        uint16_t atlasPageIndex,
                        GSTEXTURE *texture,
                        int viewWidth,
                        int viewHeight,
                        const TilemapRenderParams &params);

} // namespace level2d

#endif
