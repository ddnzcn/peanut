#ifndef ENGINE_SCENE_SCENELOADER_HPP
#define ENGINE_SCENE_SCENELOADER_HPP

namespace engine
{

    class Scene
    {
    public:
        Scene();

        void onEnter();
        void onExit();

        void loadAssets();
    };

} // namespace engine

#endif