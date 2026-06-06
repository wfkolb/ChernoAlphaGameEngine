#pragma once

namespace engine::core::ecs    { class World; }
namespace engine::core::input  { class InputSystem; }
namespace engine::core::scene  { class SceneManager; }
namespace engine::networking   { class NetworkSystem; }
namespace engine::tools        { class SaveSystem; }

namespace engine::app {

class SystemScheduler;

struct GameContext {
    core::ecs::World*           world           = nullptr;
    SystemScheduler*            systemScheduler = nullptr;
    core::input::InputSystem*   inputSystem     = nullptr;
    core::scene::SceneManager*  sceneManager    = nullptr;
    networking::NetworkSystem*  networkSystem   = nullptr;
    tools::SaveSystem*          saveSystem      = nullptr;
};

} // namespace engine::app
