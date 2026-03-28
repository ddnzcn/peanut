#include "engine/engine.hpp"
#include "atlas2d/AtlasPack.hpp"
#include "level2d/TilemapRenderer.hpp"
#include "level2d/TilemapRuntime.hpp"
#include "platform/asset_path.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <malloc.h>

#include <tamtypes.h>

extern "C"
{
#include <dmaKit.h>
#include <kernel.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
}

namespace engine
{

  static GSGLOBAL *gs_global = nullptr;
  static atlas2d::AtlasPack atlas_pack;
  static level2d::Tilemap tilemap;

  static uint16_t atlas_page_index = 0;
  static GSTEXTURE atlas_texture = {};
  static void *atlas_page_buffer = nullptr;
  static bool atlas_texture_valid = false;
  static uint32_t atlas_vram_addr = 0;
  static uint32_t atlas_vram_size = 0;

  static bool atlas_ready = false;
  static bool tilemap_ready = false;
  static bool pad_ready = false;
  static int pad_port = 0;
  static int pad_slot = 0;
  static unsigned char pad_buffer[256] __attribute__((aligned(64)));

  static bool PadStateIsReady(int state)
  {
    return state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1;
  }

  static bool InitPad()
  {
    if (SifLoadFileInit() < 0)
    {
      std::printf("InitPad: SifLoadFileInit failed\n");
      return false;
    }

    int ret = SifLoadModule("rom0:SIO2MAN", 0, nullptr);
    if (ret < 0)
    {
      ret = SifLoadModule("rom0:XSIO2MAN", 0, nullptr);
      if (ret < 0)
      {
        std::printf("InitPad: failed to load SIO2MAN/XSIO2MAN\n");
        SifLoadFileExit();
        return false;
      }
    }

    ret = SifLoadModule("rom0:PADMAN", 0, nullptr);
    if (ret < 0)
    {
      ret = SifLoadModule("rom0:XPADMAN", 0, nullptr);
      if (ret < 0)
      {
        std::printf("InitPad: failed to load PADMAN/XPADMAN\n");
        SifLoadFileExit();
        return false;
      }
    }

    if (padInit(0) == 0)
    {
      std::printf("InitPad: padInit failed\n");
      SifLoadFileExit();
      return false;
    }

    if (padPortOpen(pad_port, pad_slot, pad_buffer) == 0)
    {
      std::printf("InitPad: padPortOpen failed\n");
      padEnd();
      SifLoadFileExit();
      return false;
    }

    std::printf("InitPad: opened pad on port=%d slot=%d\n", pad_port, pad_slot);
    return true;
  }

  static void ShutdownPad()
  {
    if (pad_ready)
    {
      padPortClose(pad_port, pad_slot);
      padEnd();
    }

    SifLoadFileExit();
    pad_ready = false;
  }

  static uint16_t ReadPadButtons()
  {
    if (!pad_ready)
    {
      return 0;
    }

    const int state = padGetState(pad_port, pad_slot);
    if (!PadStateIsReady(state))
    {
      return 0;
    }

    struct padButtonStatus buttons;
    if (padRead(pad_port, pad_slot, &buttons) == 0)
    {
      return 0;
    }

    return static_cast<uint16_t>(~buttons.btns);
  }

  static void ResetAtlasState()
  {
    atlas_ready = false;
    tilemap_ready = false;
    atlas_texture_valid = false;
    atlas_page_index = 0;
    atlas_vram_addr = 0;
    atlas_vram_size = 0;
    std::memset(&atlas_texture, 0, sizeof(atlas_texture));
    tilemap.Clear();

    if (atlas_page_buffer)
    {
      free(atlas_page_buffer);
      atlas_page_buffer = nullptr;
    }
  }

  static bool UploadAtlasPage(uint16_t pageIndex)
  {
    const atlas2d::AtlasImageView page = atlas_pack.GetPageImage(pageIndex);
    if (!page.pixels)
    {
      std::printf("UploadAtlasPage: page %u has no data\n",
                  static_cast<unsigned>(pageIndex));
      return false;
    }

    if (page.format != atlas2d::PageFormat::RGBA32)
    {
      std::printf("UploadAtlasPage: unsupported page format %u\n",
                  static_cast<unsigned>(page.format));
      return false;
    }

    const size_t expectedSize =
        static_cast<size_t>(page.width) * static_cast<size_t>(page.height) * 4u;
    if (page.sizeBytes < expectedSize)
    {
      std::printf("UploadAtlasPage: page too small size=%u expected=%u\n",
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
      std::printf("UploadAtlasPage: memalign failed\n");
      return false;
    }

    std::memcpy(atlas_page_buffer, page.pixels, expectedSize);

    const uint32_t neededVram =
        gsKit_texture_size(page.width, page.height, GS_PSM_CT32);

    if (atlas_vram_addr == 0 || atlas_vram_size < neededVram)
    {
      // gsKit VRAM is a bump allocator with no per-block free.
      // Old allocation is leaked if size changed -- unavoidable without gsKit_vram_clear.
      const uint32_t vram = gsKit_vram_alloc(gs_global, neededVram, GSKIT_ALLOC_USERBUFFER);

      if (vram == GSKIT_ALLOC_ERROR)
      {
        std::printf("UploadAtlasPage: VRAM allocation failed\n");
        free(atlas_page_buffer);
        atlas_page_buffer = nullptr;
        return false;
      }

      atlas_vram_addr = vram;
      atlas_vram_size = neededVram;
    }

    std::memset(&atlas_texture, 0, sizeof(atlas_texture));
    atlas_texture.Width = page.width;
    atlas_texture.Height = page.height;
    atlas_texture.PSM = GS_PSM_CT32;
    atlas_texture.Mem = reinterpret_cast<u32 *>(atlas_page_buffer);
    atlas_texture.Vram = atlas_vram_addr;
    atlas_texture.Clut = nullptr;
    atlas_texture.VramClut = 0;
    atlas_texture.Filter = GS_FILTER_NEAREST;
    atlas_texture.Delayed = 0;

    FlushCache(0);
    FlushCache(2);
    gsKit_texture_upload(gs_global, &atlas_texture);

    atlas_texture_valid = true;
    atlas_page_index = pageIndex;

    std::printf("UploadAtlasPage: uploaded page=%u size=%ux%u bytes=%u vram=%u\n",
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

    pad_ready = InitPad();

    gs_global = gsKit_init_global();
    if (!gs_global)
    {
      std::printf("gsKit_init_global failed\n");
      running_ = false;
      return;
    }

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
    const std::string tilemapPath = platform::ResolveAssetPath("level.tmap.bin");

    std::printf("Atlas meta path: %s\n", metaPath.c_str());
    std::printf("Atlas data path: %s\n", atlasPath.c_str());
    std::printf("Tilemap path: %s\n", tilemapPath.c_str());

    if (!atlas_pack.Load(metaPath, atlasPath))
    {
      std::printf("Atlas load failed: %s\n", atlas_pack.GetLastError().c_str());
      running_ = false;
      return;
    }

    if (!tilemap.Load(tilemapPath))
    {
      std::printf("Tilemap load failed: %s\n", tilemap.GetLastError().c_str());
      running_ = false;
      return;
    }

    const level2d::TilemapHeader *tilemapHeader = tilemap.GetHeader();
    if (!tilemapHeader)
    {
      std::printf("Tilemap header missing after load\n");
      running_ = false;
      return;
    }

    if (!UploadAtlasPage(0))
    {
      std::printf("Failed to upload atlas page 0\n");
      running_ = false;
      return;
    }

    std::printf("Tilemap loaded: size=%ux%u layers=%u chunks=%u\n",
                static_cast<unsigned>(tilemapHeader->mapWidthTiles),
                static_cast<unsigned>(tilemapHeader->mapHeightTiles),
                static_cast<unsigned>(tilemapHeader->layerCount),
                static_cast<unsigned>(tilemapHeader->chunkCount));

    atlas_ready = true;
    tilemap_ready = true;
  }

  static constexpr float TILEMAP_SCALE = 4.0f;
  static constexpr int CAMERA_SPEED = 2;
  static constexpr uint8_t CLEAR_R = 8;
  static constexpr uint8_t CLEAR_G = 16;
  static constexpr uint8_t CLEAR_B = 32;
  static constexpr bool DEBUG_TILE_GRID = true;
  static constexpr bool DEBUG_CHUNK_GRID = true;

  static void DrawDebugGrid(const level2d::TilemapHeader &header,
                            const level2d::TilemapRenderParams &params)
  {
    if (!gs_global)
    {
      return;
    }

    const float tileW = static_cast<float>(header.tileWidth) * params.scale;
    const float tileH = static_cast<float>(header.tileHeight) * params.scale;
    const float chunkW = static_cast<float>(header.chunkWidthTiles) * tileW;
    const float chunkH = static_cast<float>(header.chunkHeightTiles) * tileH;

    const float scrollX = static_cast<float>(params.cameraX) * params.scale;
    const float scrollY = static_cast<float>(params.cameraY) * params.scale;

    const u64 tileColor = GS_SETREG_RGBAQ(255, 255, 255, 48, 0);
    const u64 chunkColor = GS_SETREG_RGBAQ(255, 200, 64, 96, 0);
    const u64 chunkFillColor = GS_SETREG_RGBAQ(96, 180, 255, 56, 0);

    if (DEBUG_CHUNK_GRID)
    {
      const uint32_t chunkCols =
          (static_cast<uint32_t>(header.mapWidthTiles) + static_cast<uint32_t>(header.chunkWidthTiles) - 1u) /
          static_cast<uint32_t>(header.chunkWidthTiles);
      const uint32_t chunkRows =
          (static_cast<uint32_t>(header.mapHeightTiles) + static_cast<uint32_t>(header.chunkHeightTiles) - 1u) /
          static_cast<uint32_t>(header.chunkHeightTiles);

      for (uint32_t cy = 0; cy < chunkRows; ++cy)
      {
        for (uint32_t cx = 0; cx < chunkCols; ++cx)
        {
          if (((cx + cy) & 1u) == 0u)
          {
            continue;
          }

          const float left = params.originX + static_cast<float>(cx) * chunkW - scrollX;
          const float top = params.originY + static_cast<float>(cy) * chunkH - scrollY;
          const float right = left + chunkW;
          const float bottom = top + chunkH;

          for (float x = left - chunkH; x < right; x += tileW)
          {
            const float x0 = (x < left) ? left : x;
            const float y0 = top + (x0 - x);
            const float y1 = (bottom < top + (right - x)) ? bottom : (top + (right - x));
            const float x1 = x + (y1 - top);
            gsKit_prim_line(gs_global, x0, y0, x1, y1, 1, chunkFillColor);
          }
        }
      }
    }

    if (DEBUG_TILE_GRID)
    {
      for (uint32_t x = 0; x <= header.mapWidthTiles; ++x)
      {
        const float sx = params.originX + static_cast<float>(x) * tileW - scrollX;
        gsKit_prim_line(gs_global, sx, 0.0f, sx, static_cast<float>(gs_global->Height), 1, tileColor);
      }

      for (uint32_t y = 0; y <= header.mapHeightTiles; ++y)
      {
        const float sy = params.originY + static_cast<float>(y) * tileH - scrollY;
        gsKit_prim_line(gs_global, 0.0f, sy, static_cast<float>(gs_global->Width), sy, 1, tileColor);
      }
    }

    if (DEBUG_CHUNK_GRID)
    {
      const uint32_t chunkCols =
          (static_cast<uint32_t>(header.mapWidthTiles) + static_cast<uint32_t>(header.chunkWidthTiles) - 1u) /
          static_cast<uint32_t>(header.chunkWidthTiles);
      const uint32_t chunkRows =
          (static_cast<uint32_t>(header.mapHeightTiles) + static_cast<uint32_t>(header.chunkHeightTiles) - 1u) /
          static_cast<uint32_t>(header.chunkHeightTiles);

      for (uint32_t cx = 0; cx <= chunkCols; ++cx)
      {
        const float sx = params.originX + static_cast<float>(cx) * chunkW - scrollX;
        gsKit_prim_line(gs_global, sx, 0.0f, sx, static_cast<float>(gs_global->Height), 1, chunkColor);
      }

      for (uint32_t cy = 0; cy <= chunkRows; ++cy)
      {
        const float sy = params.originY + static_cast<float>(cy) * chunkH - scrollY;
        gsKit_prim_line(gs_global, 0.0f, sy, static_cast<float>(gs_global->Width), sy, 1, chunkColor);
      }
    }
  }

  static int camera_x = 0;
  static int camera_y = 0;

  void Engine::tick()
  {
    ++frame_count_;

    const uint16_t buttons = ReadPadButtons();
    if ((buttons & PAD_LEFT) != 0)
    {
      camera_x -= CAMERA_SPEED;
    }
    if ((buttons & PAD_RIGHT) != 0)
    {
      camera_x += CAMERA_SPEED;
    }
    if ((buttons & PAD_UP) != 0)
    {
      camera_y -= CAMERA_SPEED;
    }
    if ((buttons & PAD_DOWN) != 0)
    {
      camera_y += CAMERA_SPEED;
    }

    const level2d::TilemapHeader *tilemapHeader = tilemap.GetHeader();
    if (tilemap_ready && tilemapHeader)
    {
      const int mapPixelWidth =
          static_cast<int>(tilemapHeader->mapWidthTiles) * static_cast<int>(tilemapHeader->tileWidth);
      const int mapPixelHeight =
          static_cast<int>(tilemapHeader->mapHeightTiles) * static_cast<int>(tilemapHeader->tileHeight);
      const int screenWorldWidth = static_cast<int>(gs_global->Width / TILEMAP_SCALE);
      const int screenWorldHeight = static_cast<int>(gs_global->Height / TILEMAP_SCALE);
      const int maxCameraX = (mapPixelWidth > screenWorldWidth) ? (mapPixelWidth - screenWorldWidth) : 0;
      const int maxCameraY = (mapPixelHeight > screenWorldHeight) ? (mapPixelHeight - screenWorldHeight) : 0;

      if (camera_x < 0)
      {
        camera_x = 0;
      }
      if (camera_x > maxCameraX)
      {
        camera_x = maxCameraX;
      }

      if (camera_y < 0)
      {
        camera_y = 0;
      }
      if (camera_y > maxCameraY)
      {
        camera_y = maxCameraY;
      }
    }

    gs_global->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_clear(gs_global, GS_SETREG_RGBAQ(CLEAR_R, CLEAR_G, CLEAR_B, 0xFF, 0x00));

    if (atlas_ready && tilemap_ready && atlas_texture_valid)
    {
      level2d::TilemapRenderParams renderParams = {};
      renderParams.cameraX = camera_x;
      renderParams.cameraY = camera_y;
      renderParams.originX = 0.0f;
      renderParams.originY = 0.0f;
      renderParams.scale = TILEMAP_SCALE;

      gs_global->PrimAlphaEnable = GS_SETTING_ON;
      gsKit_set_test(gs_global, GS_ATEST_OFF);
      gsKit_set_primalpha(gs_global, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

      for (uint32_t layerIndex = 0; layerIndex < tilemap.GetLayerCount(); ++layerIndex)
      {
        level2d::DrawVisibleLayer(gs_global,
                                  tilemap,
                                  atlas_pack,
                                  layerIndex,
                                  atlas_page_index,
                                  &atlas_texture,
                                  gs_global->Width,
                                  gs_global->Height,
                                  renderParams);
      }

      DrawDebugGrid(*tilemapHeader, renderParams);

      gs_global->PrimAlphaEnable = GS_SETTING_OFF;
    }

    gsKit_queue_exec(gs_global);
    gsKit_sync_flip(gs_global);
    gsKit_queue_reset(gs_global->Os_Queue);
  }

  void Engine::shutdown()
  {
    ResetAtlasState();
    ShutdownPad();
  }

} // namespace engine
