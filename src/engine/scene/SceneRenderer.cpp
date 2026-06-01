#include "engine/scene/SceneRenderer.hpp"
#include "atlas2d/AtlasPackUtils.hpp"

#include <cstring>

namespace engine
{

namespace
{

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

float SnapToPixel(float v)
{
  // Round to nearest integer pixel so adjacent tiles share exact edges.
  // Deterministic for a given value, so tile N's right edge == tile N+1's left edge.
  return (v >= 0.0f) ? static_cast<float>(static_cast<int>(v + 0.5f))
                     : static_cast<float>(static_cast<int>(v - 0.5f));
}

void BuildTileQuad(const atlas2d::AtlasSprite &sprite,
                   float cellX,
                   float cellY,
                   float scale,
                   atlas2d::SpriteVertex out[4])
{
  const float x0 = SnapToPixel(cellX + static_cast<float>(sprite.trimX) * scale);
  const float y0 = SnapToPixel(cellY + static_cast<float>(sprite.trimY) * scale);
  const float x1 = SnapToPixel(cellX + static_cast<float>(sprite.trimX + sprite.w) * scale);
  const float y1 = SnapToPixel(cellY + static_cast<float>(sprite.trimY + sprite.h) * scale);

  const float u0 = static_cast<float>(sprite.x);
  const float v0 = static_cast<float>(sprite.y);
  const float u1 = static_cast<float>(sprite.x + sprite.w);
  const float v1 = static_cast<float>(sprite.y + sprite.h);

  out[0] = {x0, y0, u0, v0};
  out[1] = {x1, y0, u1, v0};
  out[2] = {x1, y1, u1, v1};
  out[3] = {x0, y1, u0, v1};
}

void ApplyTileTransform(atlas2d::SpriteVertex quad[4], uint8_t transform)
{
  float u[4] = {quad[0].u, quad[1].u, quad[2].u, quad[3].u};
  float v[4] = {quad[0].v, quad[1].v, quad[2].v, quad[3].v};

  if ((transform & TILE_FLIP_X) != 0)
  {
    const float fu[4] = {u[1], u[0], u[3], u[2]};
    for (int i = 0; i < 4; ++i)
    {
      u[i] = fu[i];
    }
  }

  if ((transform & TILE_FLIP_Y) != 0)
  {
    const float fv[4] = {v[3], v[2], v[1], v[0]};
    for (int i = 0; i < 4; ++i)
    {
      v[i] = fv[i];
    }
  }

  if ((transform & TILE_ROT90) != 0)
  {
    const float ru[4] = {u[3], u[0], u[1], u[2]};
    const float rv[4] = {v[3], v[0], v[1], v[2]};
    for (int i = 0; i < 4; ++i)
    {
      u[i] = ru[i];
      v[i] = rv[i];
    }
  }

  for (int i = 0; i < 4; ++i)
  {
    quad[i].u = u[i];
    quad[i].v = v[i];
  }
}

bool ResolveTileSprite(const PscnFile &file,
                       const atlas2d::AtlasPack &atlas,
                       uint32_t tileId,
                       const atlas2d::AtlasSprite **outSprite)
{
  if (tileId == 0)
  {
    return false;
  }

  for (uint32_t i = 0; i < file.GetTilesetCount(); ++i)
  {
    const PscnTilesetDef *tileset = file.GetTileset(i);
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
    if (!file.GetAtlasSpriteIdForTile(*tileset, tileId, &spriteId))
    {
      return false;
    }

    const atlas2d::AtlasSprite *sprite = atlas.FindSpriteById(spriteId);
    if (!sprite)
    {
      return false;
    }

    *outSprite = sprite;
    return true;
  }

  return false;
}

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

void RenderSpriteNode(GSGLOBAL *gsGlobal,
                      const SceneNode &node,
                      const atlas2d::AtlasSprite &sprite,
                      GSTEXTURE *texture,
                      uint16_t atlasPageIndex,
                      const SceneRenderParams &params,
                      uint8_t flipH,
                      uint8_t flipV)
{
  if (sprite.pageIndex != atlasPageIndex)
  {
    return;
  }

  const float scrollX = static_cast<float>(
      (params.cameraX * static_cast<int>(node.parallaxX)) / 256);
  const float scrollY = static_cast<float>(
      (params.cameraY * static_cast<int>(node.parallaxY)) / 256);

  const float screenX =
      static_cast<float>(node.world.worldX >> 16) * params.scale - scrollX * params.scale;
  const float screenY =
      static_cast<float>(node.world.worldY >> 16) * params.scale - scrollY * params.scale;

  atlas2d::SpriteVertex quad[4];
  BuildTileQuad(sprite, screenX, screenY, params.scale, quad);

  uint8_t transform = 0;
  if (flipH)
  {
    transform |= TILE_FLIP_X;
  }
  if (flipV)
  {
    transform |= TILE_FLIP_Y;
  }
  if (transform != 0)
  {
    ApplyTileTransform(quad, transform);
  }

  DrawQuad(gsGlobal, texture, quad);
}

void RenderTileMapNode(GSGLOBAL *gsGlobal,
                       const SceneNode &node,
                       const PscnTileMapExt &tmExt,
                       const PscnFile &file,
                       const atlas2d::AtlasPack &atlas,
                       GSTEXTURE *texture,
                       uint16_t atlasPageIndex,
                       const SceneRenderParams &params)
{
  if (tmExt.chunkCount == 0)
  {
    return;
  }

  const int chunkPixelW =
      static_cast<int>(tmExt.chunkWidthTiles) * static_cast<int>(tmExt.tileWidth);
  const int chunkPixelH =
      static_cast<int>(tmExt.chunkHeightTiles) * static_cast<int>(tmExt.tileHeight);

  if (chunkPixelW <= 0 || chunkPixelH <= 0)
  {
    return;
  }

  const int parallaxX = static_cast<int>(node.parallaxX);
  const int parallaxY = static_cast<int>(node.parallaxY);

  const int nodeWorldPixelX = node.world.worldX >> 16;
  const int nodeWorldPixelY = node.world.worldY >> 16;

  const int scrollX = static_cast<int>(
      (static_cast<int64_t>(params.cameraX) * parallaxX) / 256);
  const int scrollY = static_cast<int>(
      (static_cast<int64_t>(params.cameraY) * parallaxY) / 256);

  // Visible chunk range culling. viewW/viewH are in screen pixels, but the
  // visible region is in world pixels = screen / scale. Without dividing by
  // scale we'd over-cull (render scale^2 too many chunks -> overdraw + queue
  // overflow).
  const float invScale = (params.scale > 0.0f) ? (1.0f / params.scale) : 1.0f;
  const int viewWorldW = static_cast<int>(static_cast<float>(params.viewW) * invScale);
  const int viewWorldH = static_cast<int>(static_cast<float>(params.viewH) * invScale);

  const int visibleMinX = scrollX - nodeWorldPixelX;
  const int visibleMinY = scrollY - nodeWorldPixelY;
  const int visibleMaxX = visibleMinX + viewWorldW - 1;
  const int visibleMaxY = visibleMinY + viewWorldH - 1;

  const int mapWidthChunks =
      (tmExt.mapWidthTiles + tmExt.chunkWidthTiles - 1) / tmExt.chunkWidthTiles;
  const int mapHeightChunks =
      (tmExt.mapHeightTiles + tmExt.chunkHeightTiles - 1) / tmExt.chunkHeightTiles;

  int chunkMinX = FloorDiv(visibleMinX, chunkPixelW);
  int chunkMinY = FloorDiv(visibleMinY, chunkPixelH);
  int chunkMaxX = FloorDiv(visibleMaxX, chunkPixelW);
  int chunkMaxY = FloorDiv(visibleMaxY, chunkPixelH);

  if (chunkMinX < 0)
  {
    chunkMinX = 0;
  }
  if (chunkMinY < 0)
  {
    chunkMinY = 0;
  }
  if (chunkMaxX >= mapWidthChunks)
  {
    chunkMaxX = mapWidthChunks - 1;
  }
  if (chunkMaxY >= mapHeightChunks)
  {
    chunkMaxY = mapHeightChunks - 1;
  }

  if (chunkMaxX < chunkMinX || chunkMaxY < chunkMinY)
  {
    return;
  }

  for (uint16_t ci = 0; ci < tmExt.chunkCount; ++ci)
  {
    const uint32_t chunkIndex = tmExt.firstChunkIndex + ci;
    const PscnChunkDef *chunk = file.GetChunk(chunkIndex);
    if (!chunk)
    {
      continue;
    }

    if (static_cast<int>(chunk->chunkX) < chunkMinX ||
        static_cast<int>(chunk->chunkX) > chunkMaxX ||
        static_cast<int>(chunk->chunkY) < chunkMinY ||
        static_cast<int>(chunk->chunkY) > chunkMaxY)
    {
      continue;
    }

    const PscnTileCell *tiles = file.GetChunkTiles(*chunk);
    if (!tiles)
    {
      continue;
    }

    const float chunkBaseX =
        (static_cast<float>(nodeWorldPixelX) +
         static_cast<float>(chunk->chunkX * chunkPixelW) -
         static_cast<float>(scrollX)) *
        params.scale;
    const float chunkBaseY =
        (static_cast<float>(nodeWorldPixelY) +
         static_cast<float>(chunk->chunkY * chunkPixelH) -
         static_cast<float>(scrollY)) *
        params.scale;

    for (uint32_t ti = 0; ti < chunk->tileCount; ++ti)
    {
      PscnTileCell tile;
      std::memcpy(&tile, &tiles[ti], sizeof(PscnTileCell));

      if (tile.tileId == 0)
      {
        continue;
      }

      const atlas2d::AtlasSprite *sprite = nullptr;
      if (!ResolveTileSprite(file, atlas, tile.tileId, &sprite))
      {
        continue;
      }

      if (!sprite || sprite->pageIndex != atlasPageIndex)
      {
        continue;
      }

      // Animated tile substitution
      if (atlas.GetAnimTileCount() > 0)
      {
        const uint32_t baseSpriteIndex =
            static_cast<uint32_t>(sprite - atlas.GetSprites());
        const uint32_t resolvedIndex =
            atlas.ResolveAnimTileFrame(baseSpriteIndex, params.timeMs);
        if (resolvedIndex != baseSpriteIndex)
        {
          const atlas2d::AtlasSprite *resolved =
              atlas.GetSpriteByIndex(resolvedIndex);
          if (resolved && resolved->pageIndex == atlasPageIndex)
          {
            sprite = resolved;
          }
        }
      }

      const uint32_t localTileX = ti % tmExt.chunkWidthTiles;
      const uint32_t localTileY = ti / tmExt.chunkWidthTiles;

      const float tileScreenX =
          chunkBaseX +
          static_cast<float>(localTileX * tmExt.tileWidth) * params.scale;
      const float tileScreenY =
          chunkBaseY +
          static_cast<float>(localTileY * tmExt.tileHeight) * params.scale;

      atlas2d::SpriteVertex quad[4];
      BuildTileQuad(*sprite, tileScreenX, tileScreenY, params.scale, quad);
      ApplyTileTransform(quad, tile.flags);
      DrawQuad(gsGlobal, texture, quad);
    }
  }
}

} // namespace

void RenderScene(GSGLOBAL *gsGlobal,
                 const SceneTree &tree,
                 const PscnFile &file,
                 const atlas2d::AtlasPack &atlas,
                 GSTEXTURE *texture,
                 uint16_t atlasPageIndex,
                 const SceneRenderParams &params)
{
  if (!gsGlobal || !texture)
  {
    return;
  }

  const uint16_t *order = tree.GetRenderOrder();
  const uint16_t count = tree.GetRenderOrderCount();

  for (uint16_t i = 0; i < count; ++i)
  {
    const SceneNode *node = tree.GetNode(order[i]);
    if (!node)
    {
      continue;
    }

    if ((node->flags & NODE_FLAG_VISIBLE) == 0)
    {
      continue;
    }
    if ((node->flags & NODE_FLAG_RUNTIME_DEAD) != 0)
    {
      continue;
    }

    switch (node->nodeType)
    {
    case NODE_SPRITE:
    {
      const PscnSpriteExt *ext = tree.GetExtensionAs<PscnSpriteExt>(*node);
      if (!ext)
      {
        break;
      }

      const atlas2d::AtlasSprite *sprite = atlas.FindSpriteById(ext->spriteId);
      if (!sprite)
      {
        break;
      }

      RenderSpriteNode(gsGlobal, *node, *sprite, texture,
                        atlasPageIndex, params, ext->flipH, ext->flipV);
      break;
    }

    case NODE_ANIMATED_SPRITE:
    {
      const PscnAnimatedSpriteExt *ext =
          tree.GetExtensionAs<PscnAnimatedSpriteExt>(*node);
      if (!ext)
      {
        break;
      }

      const atlas2d::AtlasSprite *sprite = nullptr;

      uint8_t animCount = 0;
      const uint32_t *animHashes = tree.GetAnimNameHashes(*node, &animCount);

      if (animHashes && animCount > 0)
      {
        const uint8_t idx = (node->activeAnimIndex < animCount)
                                ? node->activeAnimIndex
                                : 0;
        const uint32_t hash = animHashes[idx];
        if (hash != 0)
        {
          const uint32_t spriteIndex =
              atlas.ResolveAnimFrame(hash, params.timeMs);
          if (spriteIndex != UINT32_MAX)
          {
            sprite = atlas.GetSpriteByIndex(spriteIndex);
          }
        }
      }

      if (!sprite)
      {
        sprite = atlas.FindSpriteById(ext->defaultSpriteId);
      }

      if (!sprite)
      {
        break;
      }

      RenderSpriteNode(gsGlobal, *node, *sprite, texture,
                        atlasPageIndex, params, ext->flipH, ext->flipV);
      break;
    }

    case NODE_TILEMAP:
    {
      const PscnTileMapExt *ext = tree.GetExtensionAs<PscnTileMapExt>(*node);
      if (!ext)
      {
        break;
      }

      RenderTileMapNode(gsGlobal, *node, *ext, file, atlas,
                         texture, atlasPageIndex, params);
      break;
    }

    default:
      break;
    }
  }
}

} // namespace engine
