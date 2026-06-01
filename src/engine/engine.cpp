#include "engine/engine.hpp"
#include "engine/Signals.hpp"
#include "engine/scene/SignalBus.hpp"
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

static uint16_t atlas_page_index = 0;
static GSTEXTURE atlas_texture = {};
static void *atlas_page_buffer = nullptr;
static bool atlas_texture_valid = false;
static uint32_t atlas_vram_addr = 0;
static uint32_t atlas_vram_size = 0;

static bool sif_load_file_ready = false;
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
  sif_load_file_ready = true;

  int ret = SifLoadModule("rom0:SIO2MAN", 0, nullptr);
  if (ret < 0)
  {
    ret = SifLoadModule("rom0:XSIO2MAN", 0, nullptr);
    if (ret < 0)
    {
      std::printf("InitPad: failed to load SIO2MAN/XSIO2MAN\n");
      SifLoadFileExit();
      sif_load_file_ready = false;
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
      sif_load_file_ready = false;
      return false;
    }
  }

  if (padInit(0) == 0)
  {
    std::printf("InitPad: padInit failed\n");
    SifLoadFileExit();
    sif_load_file_ready = false;
    return false;
  }

  if (padPortOpen(pad_port, pad_slot, pad_buffer) == 0)
  {
    std::printf("InitPad: padPortOpen failed\n");
    padEnd();
    SifLoadFileExit();
    sif_load_file_ready = false;
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

  if (sif_load_file_ready)
  {
    SifLoadFileExit();
    sif_load_file_ready = false;
  }

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

static bool UploadAtlasPage(const atlas2d::AtlasPack &atlasPack, uint16_t pageIndex)
{
  const atlas2d::AtlasImageView page = atlasPack.GetPageImage(pageIndex);
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

  std::printf("UploadAtlasPage: uploaded page=%u size=%ux%u vram=%u\n",
              static_cast<unsigned>(pageIndex),
              static_cast<unsigned>(atlas_texture.Width),
              static_cast<unsigned>(atlas_texture.Height),
              static_cast<unsigned>(atlas_texture.Vram));

  return true;
}

Engine::Engine() : running_(false), frame_count_(0) {}

static void HaltWithColor(uint8_t r, uint8_t g, uint8_t b)
{
  if (!gs_global)
  {
    return;
  }

  for (int i = 0; i < 300; ++i)
  {
    gsKit_clear(gs_global, GS_SETREG_RGBAQ(r, g, b, 0xFF, 0x00));
    gsKit_queue_exec(gs_global);
    gsKit_sync_flip(gs_global);
    gsKit_queue_reset(gs_global->Os_Queue);
  }
}

void Engine::run()
{
  initHardware();
  if (!running_)
  {
    return;
  }

  const game::GameConfig config = game::Configure();

  if (!loadAssets(config))
  {
    HaltWithColor(0xFF, 0x00, 0x00);
    shutdown();
    return;
  }

  m_gameCtx.sceneTree = &m_sceneTree;
  m_gameCtx.behaviorRegistry = &m_behaviorRegistry;
  m_gameCtx.signalBus = &m_sceneTree.GetSignalBus();

  m_padPressedHash = engine::signal::Hash(engine::signal::kPadPressed);
  m_padReleasedHash = engine::signal::Hash(engine::signal::kPadReleased);

  game::Init(m_gameCtx);

  if (!swapScene(config.scenePath))
  {
    HaltWithColor(0x00, 0x00, 0xFF);
    shutdown();
    return;
  }

  while (running_)
  {
    tick();
  }

  shutdown();
}

void Engine::initHardware()
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

  // Larger oneshot queue: a full tile layer can be 1500+ textured quads, and
  // the default 1MB pool overflows when multiple layers + sprites draw in one
  // frame (overflow corrupts the DMA buffer and hangs). 4MB gives headroom.
  gs_global = gsKit_init_global_custom(4 * 1024 * 1024, GS_RENDER_QUEUE_PER_POOLSIZE);
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
}

bool Engine::loadAssets(const game::GameConfig &config)
{
  const std::string metaPath = platform::ResolveAssetPath(config.atlasMetaPath);
  const std::string atlasPath = platform::ResolveAssetPath(config.atlasDataPath);
  const std::string scenePath = platform::ResolveAssetPath(config.scenePath);

  std::printf("[peanut] atlas meta: %s\n", metaPath.c_str());
  std::printf("[peanut] atlas data: %s\n", atlasPath.c_str());
  std::printf("[peanut] scene:      %s\n", scenePath.c_str());

  if (!m_atlasPack.Load(metaPath, atlasPath))
  {
    std::printf("[peanut] FAIL atlas: %s\n", m_atlasPack.GetLastError().c_str());
    HaltWithColor(0xFF, 0x00, 0x00);
    running_ = false;
    return false;
  }

  if (!UploadAtlasPage(m_atlasPack, config.atlasPageIndex))
  {
    std::printf("Failed to upload atlas page %u\n",
                static_cast<unsigned>(config.atlasPageIndex));
    HaltWithColor(0xFF, 0x80, 0x00);
    running_ = false;
    return false;
  }

  (void)scenePath; // scene is loaded later via swapScene() so it can be swapped at runtime.

  return true;
}

bool Engine::swapAtlas(const char *metaPath, const char *dataPath, uint16_t pageIndex)
{
  if (!metaPath || !dataPath)
  {
    return false;
  }

  const std::string meta = platform::ResolveAssetPath(metaPath);
  const std::string data = platform::ResolveAssetPath(dataPath);
  std::printf("[atlas] swapping to %s + %s (page %u)\n",
              meta.c_str(), data.c_str(), static_cast<unsigned>(pageIndex));

  m_atlasPack.Clear();
  atlas_texture_valid = false;

  if (!m_atlasPack.Load(meta, data))
  {
    std::printf("[atlas] swap load failed: %s\n", m_atlasPack.GetLastError().c_str());
    return false;
  }

  if (!UploadAtlasPage(m_atlasPack, pageIndex))
  {
    std::printf("[atlas] swap upload failed\n");
    return false;
  }
  return true;
}

bool Engine::swapScene(const char *scenePath)
{
  if (!scenePath || scenePath[0] == '\0')
  {
    return false;
  }

  // 1. Tear down the current scene (if any). DestroyAll fires every behavior's
  //    onDestroy; ClearBindings wipes instances but keeps the vtable registry
  //    intact (vtables are types, registered once at boot). SceneTree.Clear()
  //    clears nodes + signal bus + behavior state arena together.
  if (m_pscnFile.IsValid())
  {
    m_behaviorRegistry.DestroyAll(m_sceneTree, &m_gameCtx);
    m_behaviorRegistry.ClearBindings();
    m_sceneTree.Clear();
    m_pscnFile.Clear();
  }

  // 2. Resolve + load the new scene.
  const std::string resolved = platform::ResolveAssetPath(scenePath);
  std::printf("[scene] loading %s\n", resolved.c_str());

  if (!m_pscnFile.Load(resolved))
  {
    std::printf("[scene] load failed: %s\n", m_pscnFile.GetLastError().c_str());
    return false;
  }

  if (!m_sceneTree.BuildFromPscn(m_pscnFile))
  {
    std::printf("[scene] tree build failed\n");
    m_pscnFile.Clear();
    return false;
  }

  std::printf("[scene] nodes=%u tilesets=%u chunks=%u strings=%u\n",
              static_cast<unsigned>(m_pscnFile.GetNodeCount()),
              static_cast<unsigned>(m_pscnFile.GetTilesetCount()),
              static_cast<unsigned>(m_pscnFile.GetChunkCount()),
              static_cast<unsigned>(m_pscnFile.GetStringCount()));

  // 3. Per-scene game-side setup (world bounds, render scale tweaks, etc.).
  game::OnSceneLoaded(m_gameCtx);

  // 4. Re-bind behaviors and run InitAll on the new instances.
  m_behaviorRegistry.BindScene(m_sceneTree, m_pscnFile);
  m_behaviorRegistry.InitAll(m_sceneTree, m_pscnFile, &m_gameCtx);

  std::printf("[scene] behaviors: %u vtables, %u instances, %u arena bytes\n",
              static_cast<unsigned>(m_behaviorRegistry.GetVTableCount()),
              static_cast<unsigned>(m_behaviorRegistry.GetInstanceCount()),
              static_cast<unsigned>(m_sceneTree.GetBehaviorArenaUsed()));

  // 5. Broadcast scene.ready so any behavior subscribed to it can react.
  SignalPayload p;
  p.kind = SIGNAL_EMPTY;
  m_sceneTree.GetSignalBus().Emit(engine::signal::Hash(engine::signal::kSceneReady),
                                  SIGNAL_TARGET_BROADCAST, p);
  return true;
}

void Engine::tick()
{
  ++frame_count_;

  // --- input read + edge detection ---
  const uint16_t prev = m_prevPadButtons;
  const uint16_t now = ReadPadButtons();
  m_gameCtx.padButtons = now;
  m_prevPadButtons = now;
  m_gameCtx.viewW = gs_global->Width;
  m_gameCtx.viewH = gs_global->Height;

  const uint16_t pressed = now & static_cast<uint16_t>(~prev);
  const uint16_t released = static_cast<uint16_t>(~now) & prev;
  SignalBus &bus = m_sceneTree.GetSignalBus();
  for (int bit = 0; bit < 16; ++bit)
  {
    const uint16_t mask = static_cast<uint16_t>(1u << bit);
    if ((pressed & mask) != 0)
    {
      SignalPayload p;
      p.kind = SIGNAL_BUTTON;
      p.u.button.mask = mask;
      p.u.button.edge = 1;
      bus.Emit(m_padPressedHash, SIGNAL_TARGET_BROADCAST, p);
    }
    if ((released & mask) != 0)
    {
      SignalPayload p;
      p.kind = SIGNAL_BUTTON;
      p.u.button.mask = mask;
      p.u.button.edge = 0;
      bus.Emit(m_padReleasedHash, SIGNAL_TARGET_BROADCAST, p);
    }
  }

  // Behaviors modify local transforms; recompute world transforms from the
  // updated locals; then game::Update (camera follow) reads fresh world
  // positions; finally drain queued signals to subscribers.
  m_behaviorRegistry.UpdateAll(m_sceneTree, &m_gameCtx);
  m_sceneTree.ComputeWorldTransforms();
  game::Update(m_gameCtx);
  bus.Dispatch(m_behaviorRegistry, m_sceneTree, &m_gameCtx);
  // Any node marked dead via SceneTree::RemoveNode during update / signals
  // gets its behaviors' onDestroy fired here, before render.
  m_behaviorRegistry.ProcessDeadNodes(m_sceneTree, &m_gameCtx);

  gs_global->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_clear(gs_global,
              GS_SETREG_RGBAQ(m_gameCtx.clearR, m_gameCtx.clearG, m_gameCtx.clearB, 0xFF, 0x00));

  if (atlas_texture_valid)
  {
    gs_global->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_test(gs_global, GS_ATEST_OFF);
    gsKit_set_primalpha(gs_global, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    SceneRenderParams renderParams;
    renderParams.cameraX = m_gameCtx.cameraX;
    renderParams.cameraY = m_gameCtx.cameraY;
    renderParams.viewW = gs_global->Width;
    renderParams.viewH = gs_global->Height;
    renderParams.scale = m_gameCtx.renderScale;
    renderParams.timeMs = frame_count_ * 16;

    RenderScene(gs_global,
                m_sceneTree,
                m_pscnFile,
                m_atlasPack,
                &atlas_texture,
                atlas_page_index,
                renderParams);

    gs_global->PrimAlphaEnable = GS_SETTING_OFF;
  }

  gsKit_queue_exec(gs_global);
  gsKit_sync_flip(gs_global);
  gsKit_queue_reset(gs_global->Os_Queue);

  // Consume any atlas/scene swap requests game code raised this frame. We
  // capture and null the pointers before swapping so a behavior's onDestroy
  // or onInit cannot recurse into another swap inside the same tick.
  // Atlas swap goes first so the new scene resolves sprites from the new atlas.
  if (m_gameCtx.requestedAtlasMeta && m_gameCtx.requestedAtlasData)
  {
    const char *meta = m_gameCtx.requestedAtlasMeta;
    const char *data = m_gameCtx.requestedAtlasData;
    const uint16_t page = m_gameCtx.requestedAtlasPage;
    m_gameCtx.requestedAtlasMeta = nullptr;
    m_gameCtx.requestedAtlasData = nullptr;
    m_gameCtx.requestedAtlasPage = 0;
    swapAtlas(meta, data, page);
  }

  if (m_gameCtx.requestedScene)
  {
    const char *path = m_gameCtx.requestedScene;
    m_gameCtx.requestedScene = nullptr;
    swapScene(path);
  }
}

void Engine::shutdown()
{
  game::Shutdown(m_gameCtx);
  m_behaviorRegistry.DestroyAll(m_sceneTree, &m_gameCtx);
  m_behaviorRegistry.Clear();
  m_sceneTree.Clear();
  m_pscnFile.Clear();
  m_atlasPack.Clear();

  atlas_texture_valid = false;
  if (atlas_page_buffer)
  {
    free(atlas_page_buffer);
    atlas_page_buffer = nullptr;
  }

  ShutdownPad();
}

} // namespace engine
