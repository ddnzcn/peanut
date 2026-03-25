#ifndef ENGINE_ECS_HPP
#define ENGINE_ENGINE_HPP

namespace ecs
{

    class Scene
    {
    public:
        Scene();

        void onEnter();
        void onExit();

        void loadAssets();
    }

}

#endif