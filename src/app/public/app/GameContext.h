#pragma once

namespace engine::core::ecs    { class World; }
namespace engine::core::input  { class InputSystem; }
namespace engine::core::scene  { class SceneManager; }
namespace engine::rendering    { class RenderSystem; }
namespace engine::networking   { class NetworkSystem; }
namespace engine::tools        { class SaveSystem; class AssetSystem; }

namespace engine::app {

class SystemScheduler;

struct GameContext {
    core::ecs::World*           world           = nullptr;
    SystemScheduler*            systemScheduler = nullptr;
    core::input::InputSystem*   inputSystem     = nullptr;
    core::scene::SceneManager*  sceneManager    = nullptr;
    rendering::RenderSystem*    renderSystem    = nullptr;
    networking::NetworkSystem*  networkSystem   = nullptr;
    tools::SaveSystem*          saveSystem      = nullptr;
    tools::AssetSystem*         assetSystem     = nullptr;
};

} // namespace engine::app
