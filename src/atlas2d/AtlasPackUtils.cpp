#include "atlas2d/AtlasPackUtils.hpp"

#include <math.h>

namespace atlas2d
{

void BuildAtlasQuad(const AtlasSprite &sprite,
                    float anchorX,
                    float anchorY,
                    float scale,
                    SpriteVertex out[4])
{
    const float x0 =
        anchorX + (static_cast<float>(sprite.trimX) - static_cast<float>(sprite.pivotX)) * scale;
    const float y0 =
        anchorY + (static_cast<float>(sprite.trimY) - static_cast<float>(sprite.pivotY)) * scale;
    const float x1 = x0 + static_cast<float>(sprite.w) * scale;
    const float y1 = y0 + static_cast<float>(sprite.h) * scale;

    // gsKit expects pixel-space texcoords (not normalized [0,1])
    const float u0 = static_cast<float>(sprite.x);
    const float v0 = static_cast<float>(sprite.y);
    const float u1 = static_cast<float>(sprite.x + sprite.w);
    const float v1 = static_cast<float>(sprite.y + sprite.h);

    out[0] = {x0, y0, u0, v0}; // TL
    out[1] = {x1, y0, u1, v0}; // TR
    out[2] = {x1, y1, u1, v1}; // BR
    out[3] = {x0, y1, u0, v1}; // BL
}

void RotateAtlasQuadUVs(SpriteVertex quad[4], SpriteRotation rotation)
{
    const float u0 = quad[0].u;
    const float v0 = quad[0].v;
    const float u1 = quad[1].u;
    const float v1 = quad[2].v;

    switch (rotation)
    {
    case SpriteRotation::None:
        quad[0].u = u0;
        quad[0].v = v0;
        quad[1].u = u1;
        quad[1].v = v0;
        quad[2].u = u1;
        quad[2].v = v1;
        quad[3].u = u0;
        quad[3].v = v1;
        break;

    case SpriteRotation::CW90:
        quad[0].u = u0;
        quad[0].v = v1;
        quad[1].u = u0;
        quad[1].v = v0;
        quad[2].u = u1;
        quad[2].v = v0;
        quad[3].u = u1;
        quad[3].v = v1;
        break;

    case SpriteRotation::CCW90:
        quad[0].u = u1;
        quad[0].v = v0;
        quad[1].u = u1;
        quad[1].v = v1;
        quad[2].u = u0;
        quad[2].v = v1;
        quad[3].u = u0;
        quad[3].v = v0;
        break;

    case SpriteRotation::Deg180:
        quad[0].u = u1;
        quad[0].v = v1;
        quad[1].u = u0;
        quad[1].v = v1;
        quad[2].u = u0;
        quad[2].v = v0;
        quad[3].u = u1;
        quad[3].v = v0;
        break;
    }
}

void RotateAtlasQuadPositions(SpriteVertex quad[4],
                              float pivotX,
                              float pivotY,
                              float radians)
{
    const float s = sinf(radians);
    const float c = cosf(radians);

    for (int i = 0; i < 4; ++i)
    {
        const float dx = quad[i].x - pivotX;
        const float dy = quad[i].y - pivotY;

        quad[i].x = pivotX + dx * c - dy * s;
        quad[i].y = pivotY + dx * s + dy * c;
    }
}

void DrawAtlasSprite(
    GSGLOBAL *gsGlobal,
    const AtlasSprite &sprite,
    GSTEXTURE *texture,
    float anchorX,
    float anchorY,
    float scale,
    SpriteRotation uvRotation)
{
    SpriteVertex quad[4];
    BuildAtlasQuad(sprite, anchorX, anchorY, scale, quad);
    RotateAtlasQuadUVs(quad, uvRotation);

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

void DrawAtlasSpriteRotated(
    GSGLOBAL *gsGlobal,
    const AtlasSprite &sprite,
    GSTEXTURE *texture,
    float anchorX,
    float anchorY,
    float scale,
    float radians,
    SpriteRotation uvRotation)
{
    SpriteVertex quad[4];
    BuildAtlasQuad(sprite, anchorX, anchorY, scale, quad);
    RotateAtlasQuadUVs(quad, uvRotation);
    RotateAtlasQuadPositions(quad, anchorX, anchorY, radians);

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

} // namespace atlas2d
