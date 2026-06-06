#pragma once
#include "app/ApplicationDesc.h"
#include "app/GameContext.h"
#include "app/GameLoop.h"
#include "app/SystemScheduler.h"
#include <core/diag/Assert.h>
#include <core/scene/SceneManager.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::physics   { class PhysicsWorld; }
namespace engine::rendering { class MeshManager;  }

namespace engine::app {

class Engine;
class MeshRenderSystem;

class Application {
public:
    ENGINE_NO_COPY(Application);
    ENGINE_NO_MOVE(Application);

    Application();
    ~Application();

    bool init(const ApplicationDesc& desc);
    void run();
    void shutdown();

private:
    struct PendingMeshLoad {
        uint32_t    entityIndex;
        std::string assetPath;
    };

    std::unique_ptr<Engine>                    engine_;
    std::unique_ptr<physics::PhysicsWorld>     physicsWorld_;
    std::unique_ptr<rendering::MeshManager>    meshManager_;     // lazy-init inside first frame
    std::unique_ptr<MeshRenderSystem>          meshRenderSystem_;
    core::scene::SceneManager                  sceneManager_;
    std::vector<PendingMeshLoad>               pendingMeshLoads_;
    SystemScheduler                            scheduler_;
    GameLoop                                   gameLoop_;
    GameContext                                context_;
    IGame*                                     game_        = nullptr;
    bool                                       initialized_ = false;
    uint16_t                                   hostPort_    = 0;
    std::string                                connectAddr_;

    void wireScene(core::scene::Scene& scene);
};

} // namespace engine::app
