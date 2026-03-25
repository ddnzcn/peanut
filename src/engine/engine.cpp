#include "engine/engine.hpp"
#include "atlas2d/AtlasPack.hpp"
#include "atlas2d/AtlasPackUtils.hpp"
#include "platform/asset_path.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <malloc.h>

#include <tamtypes.h>

extern "C"
{
#include <dmaKit.h>
#include <gsKit.h>
#include <kernel.h>
#include <sifrpc.h>
}

namespace engine
{

  static GSGLOBAL *gs_global = nullptr;
  static atlas2d::AtlasPack atlas_pack;

  static atlas2d::AtlasSprite atlas_sprite = {};
  static bool atlas_sprite_valid = false;

  static GSTEXTURE atlas_texture = {};
  static void *atlas_page_buffer = nullptr;
  static bool atlas_texture_valid = false;

  static bool atlas_ready = false;

  static void ResetAtlasState()
  {
    atlas_ready = false;
    atlas_sprite_valid = false;
    atlas_texture_valid = false;
    atlas_sprite = {};
    std::memset(&atlas_texture, 0, sizeof(atlas_texture));

    if (atlas_page_buffer)
    {
      free(atlas_page_buffer);
      atlas_page_buffer = nullptr;
    }
  }

  static bool UploadSpritePage(uint16_t pageIndex)
  {
    const atlas2d::AtlasImageView page = atlas_pack.GetPageImage(pageIndex);
    if (!page.pixels)
    {
      std::printf("UploadSpritePage: page %u has no data\n",
                  static_cast<unsigned>(pageIndex));
      return false;
    }

    if (page.format != atlas2d::PageFormat::RGBA32)
    {
      std::printf("UploadSpritePage: unsupported page format %u\n",
                  static_cast<unsigned>(page.format));
      return false;
    }

    const size_t expectedSize =
        static_cast<size_t>(page.width) * static_cast<size_t>(page.height) * 4u;
    if (page.sizeBytes < expectedSize)
    {
      std::printf("UploadSpritePage: page too small size=%u expected=%u\n",
                  static_cast<unsigned>(page.sizeBytes),
                  static_cast<unsigned>(expectedSize));
      return false;
    }

    if (atlas_page_buffer)
    {
      free(atlas_page_buffer);
      atlas_page_buffer = nullptr;
    }

    atlas_page_buffer = memalign(128, expectedSize);
    if (!atlas_page_buffer)
    {
      std::printf("UploadSpritePage: memalign failed\n");
      return false;
    }

    std::memcpy(atlas_page_buffer, page.pixels, expectedSize);

    std::memset(&atlas_texture, 0, sizeof(atlas_texture));
    atlas_texture.Width = page.width;
    atlas_texture.Height = page.height;
    atlas_texture.PSM = GS_PSM_CT32;
    atlas_texture.Mem = reinterpret_cast<u32 *>(atlas_page_buffer);
    atlas_texture.Clut = nullptr;
    atlas_texture.VramClut = 0;
    atlas_texture.Filter = GS_FILTER_NEAREST;
    atlas_texture.Delayed = 0;

    atlas_texture.Vram = gsKit_vram_alloc(
        gs_global,
        gsKit_texture_size(atlas_texture.Width, atlas_texture.Height, atlas_texture.PSM),
        GSKIT_ALLOC_USERBUFFER);

    if (atlas_texture.Vram == GSKIT_ALLOC_ERROR)
    {
      std::printf("UploadSpritePage: VRAM allocation failed\n");
      return false;
    }

    FlushCache(0);
    FlushCache(2);
    gsKit_texture_upload(gs_global, &atlas_texture);

    atlas_texture_valid = true;

    std::printf("UploadSpritePage: uploaded page=%u size=%ux%u bytes=%u vram=%u\n",
                static_cast<unsigned>(pageIndex),
                static_cast<unsigned>(atlas_texture.Width),
                static_cast<unsigned>(atlas_texture.Height),
                static_cast<unsigned>(expectedSize),
                static_cast<unsigned>(atlas_texture.Vram));

    return true;
  }

  Engine::Engine() : running_(false), frame_count_(0) {}

  void Engine::run()
  {
    init();

    while (running_)
    {
      tick();
    }

    shutdown();
  }

  void Engine::init()
  {
    sceSifInitRpc(0);

    running_ = true;
    frame_count_ = 0;

    dmaKit_init(
        D_CTRL_RELE_OFF,
        D_CTRL_MFD_OFF,
        D_CTRL_STS_UNSPEC,
        D_CTRL_STD_OFF,
        D_CTRL_RCYC_8,
        0);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gs_global = gsKit_init_global();

    gs_global->Mode = GS_MODE_PAL;
    gs_global->Interlace = GS_INTERLACED;
    gs_global->Field = GS_FIELD;
    gs_global->Width = 640;
    gs_global->Height = 512;
    gs_global->DoubleBuffering = GS_SETTING_ON;
    gs_global->ZBuffering = GS_SETTING_OFF;
    gs_global->PrimAlphaEnable = GS_SETTING_OFF;
    gs_global->PSM = GS_PSM_CT32;
    gs_global->PSMZ = GS_PSMZ_16S;

    gsKit_init_screen(gs_global);
    gsKit_mode_switch(gs_global, GS_ONESHOT);
    gsKit_set_clamp(gs_global, GS_CMODE_CLAMP);

    ResetAtlasState();

    const std::string metaPath = platform::ResolveAssetPath("atlas.meta.bin");
    const std::string atlasPath = platform::ResolveAssetPath("atlas.bin");

    std::printf("Atlas meta path: %s\n", metaPath.c_str());
    std::printf("Atlas data path: %s\n", atlasPath.c_str());

    if (!atlas_pack.Load(metaPath, atlasPath))
    {
      std::printf("Atlas load failed: %s\n", atlas_pack.GetLastError().c_str());
      return;
    }

    const atlas2d::AtlasSprite *spritePtr = atlas_pack.GetSpriteByIndex(0);
    if (!spritePtr)
    {
      std::printf("Sprite 0 not found\n");
      return;
    }

    atlas_sprite = *spritePtr;
    atlas_sprite_valid = true;

    std::printf("Sprite 0: page=%u rect=(%u,%u,%u,%u) source=(%u,%u) trim=(%d,%d) pivot=(%d,%d)\n",
                static_cast<unsigned>(atlas_sprite.pageIndex),
                static_cast<unsigned>(atlas_sprite.x),
                static_cast<unsigned>(atlas_sprite.y),
                static_cast<unsigned>(atlas_sprite.w),
                static_cast<unsigned>(atlas_sprite.h),
                static_cast<unsigned>(atlas_sprite.sourceW),
                static_cast<unsigned>(atlas_sprite.sourceH),
                static_cast<int>(atlas_sprite.trimX),
                static_cast<int>(atlas_sprite.trimY),
                static_cast<int>(atlas_sprite.pivotX),
                static_cast<int>(atlas_sprite.pivotY));

    if (!UploadSpritePage(atlas_sprite.pageIndex))
    {
      std::printf("Failed to upload sprite page\n");
      atlas_sprite_valid = false;
      return;
    }

    atlas_ready = true;
  }

  static float sprite_angle = 0.0f;

  void Engine::tick()
  {
    ++frame_count_;

    sprite_angle += 0.2f;
    if (sprite_angle > 6.2831853f)
    {
      sprite_angle -= 6.2831853f;
    }

    gs_global->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_clear(gs_global, GS_SETREG_RGBAQ(8, 16, 32, 0xFF, 0x00));

    if (atlas_ready && atlas_sprite_valid && atlas_texture_valid)
    {
      gs_global->PrimAlphaEnable = GS_SETTING_ON;
      gsKit_set_test(gs_global, GS_ATEST_OFF);
      gsKit_set_primalpha(gs_global, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

      atlas2d::DrawAtlasSpriteRotated(
          gs_global,
          atlas_sprite,
          &atlas_texture,
          320.0f,
          256.0f,
          8.0f,
          sprite_angle);

      gs_global->PrimAlphaEnable = GS_SETTING_OFF;
    }

    gsKit_queue_exec(gs_global);
    gsKit_sync_flip(gs_global);
    gsKit_queue_reset(gs_global->Os_Queue);
  }

  void Engine::shutdown()
  {
    ResetAtlasState();
  }

} // namespace engine
