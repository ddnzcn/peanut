#ifndef ATLAS2D_ATLASPACKUTILS_HPP
#define ATLAS2D_ATLASPACKUTILS_HPP

#include "AtlasPack.hpp"

extern "C"
{
#include <gsKit.h>
}

namespace atlas2d
{
    struct SpriteVertex
    {
        float x;
        float y;
        float u;
        float v;
    };

    enum class SpriteRotation
    {
        None,
        CW90,
        CCW90,
        Deg180
    };

    void BuildAtlasQuad(const AtlasSprite &sprite,
                        float anchorX,
                        float anchorY,
                        float scale,
                        SpriteVertex out[4]);

    void RotateAtlasQuadUVs(SpriteVertex quad[4], SpriteRotation rotation);

    void RotateAtlasQuadPositions(SpriteVertex quad[4],
                                  float pivotX,
                                  float pivotY,
                                  float radians);

    void DrawAtlasSprite(
        GSGLOBAL *gsGlobal,
        const AtlasSprite &sprite,
        GSTEXTURE *texture,
        float anchorX,
        float anchorY,
        float scale,
        SpriteRotation uvRotation = SpriteRotation::None);

    void DrawAtlasSpriteRotated(
        GSGLOBAL *gsGlobal,
        const AtlasSprite &sprite,
        GSTEXTURE *texture,
        float anchorX,
        float anchorY,
        float scale,
        float radians,
        SpriteRotation uvRotation = SpriteRotation::None);

} // namespace atlas2d

#endif
