#ifndef GAME_BEHAVIORS_HPP
#define GAME_BEHAVIORS_HPP

namespace engine
{
class BehaviorRegistry;
} // namespace engine

namespace game
{

// One-stop helper: cast the void* state slot back to your behavior's typed
// state struct. Zero runtime cost — purely a type-system operation.
template <typename T>
inline T &S(void *state)
{
  return *static_cast<T *>(state);
}

void RegisterAllBehaviors(engine::BehaviorRegistry &registry);

} // namespace game

#endif
