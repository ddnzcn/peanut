#ifndef ENGINE_SIGNALS_HPP
#define ENGINE_SIGNALS_HPP

#include "atlas2d/AtlasPack.hpp"
#include "engine/scene/BehaviorRegistry.hpp"

#include <cstdint>

namespace engine
{
namespace signal
{

// Engine-reserved signal names. Game code may use any string not in the
// reserved namespaces ("pad.*", "scene.*").
inline constexpr const char *kPadPressed = "pad.pressed";
inline constexpr const char *kPadReleased = "pad.released";
inline constexpr const char *kSceneReady = "scene.ready";

inline uint32_t Hash(const char *s)
{
  return atlas2d::FNV1a32(s);
}

inline bool Subscribe(BehaviorRegistry &reg, uint16_t bindingIndex, const char *name)
{
  return reg.Subscribe(bindingIndex, Hash(name));
}

} // namespace signal
} // namespace engine

#endif
