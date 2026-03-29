#include "level2d/TilemapRenderer.hpp"

#include "atlas2d/AtlasPackUtils.hpp"

namespace
{

  // Matches TilemapRuntime's FloorDiv — needed here so parallax scroll calculation
  // is consistent with chunk visibility culling (was using truncating integer division before)
  int FloorDiv(int value, int divisor)
  {
    if (divisor <= 0)
    {
      return 0;
    }

    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && value < 0)
    {
      --quotient;
    }

    return quotient;
  }

} // namespace

namespace level2d
{

  namespace
  {

    bool ResolveTileSprite(const Tilemap &tilemap,
                           const atlas2d::AtlasPack &atlasPack,
                           uint32_t tileId,
                           const TilesetDef **outTileset,
                           const atlas2d::AtlasSprite **outSprite)
    {
      if (!outTileset || !outSprite)
      {
        return false;
      }

      *outTileset = nullptr;
      *outSprite = nullptr;

      if (tileId == 0)
      {
        return false;
      }

      for (uint32_t i = 0; i < tilemap.GetTilesetCount(); ++i)
      {
        const TilesetDef *tileset = tilemap.GetTilesetByIndex(i);
        if (!tileset)
        {
          continue;
        }

        if (tileId < tileset->firstTileId ||
            tileId >= (tileset->firstTileId + tileset->tileCount))
        {
          continue;
        }

        uint32_t spriteId = 0;
        if (!tilemap.GetAtlasSpriteIdForTile(*tileset, tileId, &spriteId))
        {
          return false;
        }

        // O(1) flat-table lookup — was O(n) linear scan per tile per frame
        const atlas2d::AtlasSprite *sprite = atlasPack.FindSpriteByIdFast(spriteId);
        if (!sprite)
        {
          return false;
        }

        *outTileset = tileset;
        *outSprite = sprite;
        return true;
      }

      return false;
    }

    void BuildTileQuad(const atlas2d::AtlasSprite &sprite,
                       const TilesetDef &tileset,
                       float cellX,
                       float cellY,
                       float scale,
                       atlas2d::SpriteVertex out[4])
    {
      const bool rotated = (sprite.flags & atlas2d::SpriteFlag_Rotated) != 0;

      // For a rotated sprite the atlas region is H×W (packed CW), so sprite.w = original H
      // and sprite.h = original W. Screen quad must show the original W×H dimensions.
      const float w = static_cast<float>(rotated ? sprite.h : sprite.w) * scale;
      const float h = static_cast<float>(rotated ? sprite.w : sprite.h) * scale;

      const float x0 = cellX + static_cast<float>(sprite.trimX) * scale;
      const float y0 = cellY + static_cast<float>(sprite.trimY) * scale;

      const float u0 = static_cast<float>(sprite.x);
      const float v0 = static_cast<float>(sprite.y);
      const float u1 = static_cast<float>(sprite.x + sprite.w);
      const float v1 = static_cast<float>(sprite.y + sprite.h);

      (void)tileset;

      out[0] = {x0, y0, u0, v0};
      out[1] = {x0 + w, y0, u1, v0};
      out[2] = {x0 + w, y0 + h, u1, v1};
      out[3] = {x0, y0 + h, u0, v1};

      if (rotated)
      {
        atlas2d::RotateAtlasQuadUVs(out, atlas2d::SpriteRotation::CW90);
      }
    }

    void ApplyTileFlags(atlas2d::SpriteVertex quad[4], uint8_t flags)
    {
      float u[4] = {quad[0].u, quad[1].u, quad[2].u, quad[3].u};
      float v[4] = {quad[0].v, quad[1].v, quad[2].v, quad[3].v};

      if ((flags & TileFlag_FlipX) != 0)
      {
        const float flippedU[4] = {u[1], u[0], u[3], u[2]};
        for (int i = 0; i < 4; ++i)
        {
          u[i] = flippedU[i];
        }
      }

      if ((flags & TileFlag_FlipY) != 0)
      {
        const float flippedV[4] = {v[3], v[2], v[1], v[0]};
        for (int i = 0; i < 4; ++i)
        {
          v[i] = flippedV[i];
        }
      }

      if ((flags & TileFlag_Rot90) != 0)
      {
        const float rotatedU[4] = {u[3], u[0], u[1], u[2]};
        const float rotatedV[4] = {v[3], v[0], v[1], v[2]};
        for (int i = 0; i < 4; ++i)
        {
          u[i] = rotatedU[i];
          v[i] = rotatedV[i];
        }
      }

      for (int i = 0; i < 4; ++i)
      {
        quad[i].u = u[i];
        quad[i].v = v[i];
      }
    }

    void DrawQuad(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const atlas2d::SpriteVertex quad[4])
    {
      const u64 color = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00);

      gsKit_prim_triangle_texture(
          gsGlobal, texture,
          quad[0].x, quad[0].y, quad[0].u, quad[0].v,
          quad[1].x, quad[1].y, quad[1].u, quad[1].v,
          quad[2].x, quad[2].y, quad[2].u, quad[2].v,
          1, color);

      gsKit_prim_triangle_texture(
          gsGlobal, texture,
          quad[0].x, quad[0].y, quad[0].u, quad[0].v,
          quad[2].x, quad[2].y, quad[2].u, quad[2].v,
          quad[3].x, quad[3].y, quad[3].u, quad[3].v,
          1, color);
    }

  } // namespace

  bool DrawChunk(GSGLOBAL *gsGlobal,
                 const Tilemap &tilemap,
                 const atlas2d::AtlasPack &atlasPack,
                 uint16_t atlasPageIndex,
                 GSTEXTURE *texture,
                 const LayerDef &layer,
                 const TileChunkView &chunkView,
                 const TilemapRenderParams &params)
  {
    if (!gsGlobal || !texture || !chunkView.hasChunk || !chunkView.tiles || !tilemap.GetHeader())
    {
      return false;
    }

    const TilemapHeader &header = *tilemap.GetHeader();
    const int parallaxX = static_cast<int>(layer.parallaxX_8_8);
    const int parallaxY = static_cast<int>(layer.parallaxY_8_8);

    // FloorDiv instead of integer division — truncation caused ±1px jitter
    // at parallax values != 256, making chunks pop in/out at boundaries
    const float scrollX =
        static_cast<float>(FloorDiv(params.cameraX * parallaxX, 256)) - static_cast<float>(layer.offsetX);
    const float scrollY =
        static_cast<float>(FloorDiv(params.cameraY * parallaxY, 256)) - static_cast<float>(layer.offsetY);

    const float chunkBaseX =
        params.originX +
        static_cast<float>(chunkView.chunk.chunkX * header.chunkWidthTiles * header.tileWidth) *
            params.scale -
        scrollX * params.scale;
    const float chunkBaseY =
        params.originY +
        static_cast<float>(chunkView.chunk.chunkY * header.chunkHeightTiles * header.tileHeight) *
            params.scale -
        scrollY * params.scale;

    for (uint32_t tileIndex = 0; tileIndex < chunkView.chunk.tileCount; ++tileIndex)
    {
      // Direct const ref — was unnecessary memcpy into stack copy
      const TileEntry &tile = chunkView.tiles[tileIndex];

      const TilesetDef *tileset = nullptr;
      const atlas2d::AtlasSprite *sprite = nullptr;
      if (!ResolveTileSprite(tilemap, atlasPack, tile.tileId, &tileset, &sprite))
      {
        continue;
      }

      if (!sprite || sprite->pageIndex != atlasPageIndex)
      {
        continue;
      }

      const uint32_t localTileX = tileIndex % header.chunkWidthTiles;
      const uint32_t localTileY = tileIndex / header.chunkWidthTiles;

      const float tileScreenX =
          chunkBaseX + static_cast<float>(localTileX * header.tileWidth) * params.scale;
      const float tileScreenY =
          chunkBaseY + static_cast<float>(localTileY * header.tileHeight) * params.scale;

      atlas2d::SpriteVertex quad[4];
      BuildTileQuad(*sprite, *tileset, tileScreenX, tileScreenY, params.scale, quad);
      ApplyTileFlags(quad, tile.flags);
      DrawQuad(gsGlobal, texture, quad);
    }

    return true;
  }

  bool DrawVisibleLayer(GSGLOBAL *gsGlobal,
                        const Tilemap &tilemap,
                        const atlas2d::AtlasPack &atlasPack,
                        uint32_t layerIndex,
                        uint16_t atlasPageIndex,
                        GSTEXTURE *texture,
                        int viewWidth,
                        int viewHeight,
                        const TilemapRenderParams &params)
  {
    if (!gsGlobal || !texture)
    {
      return false;
    }

    LayerView layerView = {};
    if (!tilemap.GetLayerView(layerIndex, &layerView) || !layerView.layer)
    {
      return false;
    }

    // Direct const ref — was unnecessary memcpy into stack copy
    const LayerDef &layer = *layerView.layer;

    if ((layer.flags & LayerFlag_Visible) == 0)
    {
      return true;
    }

    int minChunkX = 0;
    int minChunkY = 0;
    int maxChunkX = 0;
    int maxChunkY = 0;

    if (!tilemap.ComputeVisibleChunkRange(layer,
                                          params.cameraX,
                                          params.cameraY,
                                          viewWidth,
                                          viewHeight,
                                          &minChunkX,
                                          &minChunkY,
                                          &maxChunkX,
                                          &maxChunkY))
    {
      return true;
    }

    for (uint32_t i = 0; i < layer.chunkCount; ++i)
    {
      // Direct const ref — was unnecessary memcpy into stack copy
      // Explicit int cast on uint16_t fields to avoid signed/unsigned comparison
      const ChunkDef &chunk = layerView.chunks[i];
      if (static_cast<int>(chunk.chunkX) < minChunkX || static_cast<int>(chunk.chunkX) > maxChunkX ||
          static_cast<int>(chunk.chunkY) < minChunkY || static_cast<int>(chunk.chunkY) > maxChunkY)
      {
        continue;
      }

      TileChunkView chunkView = {};
      if (!tilemap.GetChunkView(layer.firstChunkIndex + i, &chunkView))
      {
        continue;
      }

      DrawChunk(gsGlobal,
                tilemap,
                atlasPack,
                atlasPageIndex,
                texture,
                layer,
                chunkView,
                params);
    }

    return true;
  }

} // namespace level2d
