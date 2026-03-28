#include "level2d/TilemapRenderer.hpp"

#include "atlas2d/AtlasPackUtils.hpp"
#include <cstring>

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

    const atlas2d::AtlasSprite *sprite = atlasPack.FindSpriteById(spriteId);
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
  const float x0 = cellX + static_cast<float>(sprite.trimX) * scale;
  const float y0 = cellY + static_cast<float>(sprite.trimY) * scale;

  const float w = static_cast<float>(sprite.w) * scale;
  const float h = static_cast<float>(sprite.h) * scale;

  const float u0 = static_cast<float>(sprite.x);
  const float v0 = static_cast<float>(sprite.y);
    const float u1 = static_cast<float>(sprite.x + sprite.w);
    const float v1 = static_cast<float>(sprite.y + sprite.h);

    (void)tileset;

  out[0] = {x0, y0, u0, v0};
  out[1] = {x0 + w, y0, u1, v0};
  out[2] = {x0 + w, y0 + h, u1, v1};
  out[3] = {x0, y0 + h, u0, v1};
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

  LayerDef layerCopy = {};
  std::memcpy(&layerCopy, &layer, sizeof(LayerDef));

  const TilemapHeader &header = *tilemap.GetHeader();
  const int parallaxX = static_cast<int>(layerCopy.parallaxX_8_8);
  const int parallaxY = static_cast<int>(layerCopy.parallaxY_8_8);

  const float scrollX =
      static_cast<float>((params.cameraX * parallaxX) / 256) - static_cast<float>(layerCopy.offsetX);
  const float scrollY =
      static_cast<float>((params.cameraY * parallaxY) / 256) - static_cast<float>(layerCopy.offsetY);

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
    TileEntry tile = {};
    std::memcpy(&tile, &chunkView.tiles[tileIndex], sizeof(TileEntry));

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

  LayerDef layer = {};
  std::memcpy(&layer, layerView.layer, sizeof(LayerDef));

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
    ChunkDef chunk = {};
    std::memcpy(&chunk, &layerView.chunks[i], sizeof(ChunkDef));
    if (chunk.chunkX < minChunkX || chunk.chunkX > maxChunkX ||
        chunk.chunkY < minChunkY || chunk.chunkY > maxChunkY)
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
