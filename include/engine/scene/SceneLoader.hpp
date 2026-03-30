#ifndef ENGINE_SCENE_SCENELOADER_HPP
#define ENGINE_SCENE_SCENELOADER_HPP

#include <vector>

namespace engine
{

    class Scene
    {
    public:
        // TODO: type this shit
        std::vector<void *> entities;
        // TODO: type this shit
        std::vector<void *> systems;

        Scene();

        void onEnter();
        void onExit();

        void loadSystem(void *system);
        void addEntity(void *entitiy);
    };

} // namespace engine

#endif