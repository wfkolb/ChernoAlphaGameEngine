#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "app/Application.h"
#include "app/IGame.h"
#include "app/Engine.h"
#include "MeshRenderSystem.h"
#include <physics/PhysicsWorld.h>
#include <rendering/MeshManager.h>
#include <tools/EassetLoader.h>
#include <core/components/ColliderComponent.h>
#include <core/log.h>
#include <algorithm>
#include <chrono>
#include <span>

namespace engine::app {

Application::Application() : engine_(std::make_unique<Engine>()) {}

Application::~Application() {
    if (initialized_) shutdown();
}

bool Application::init(const ApplicationDesc& desc) {
    EngineConfig cfg;
    cfg.windowWidth  = desc.windowWidth;
    cfg.windowHeight = desc.windowHeight;
    cfg.windowTitle  = desc.windowTitle.c_str();
    cfg.vsync        = desc.vsync;

    if (!engine_->init(cfg)) return false;

    // BW4: create physics world (required by GameLoop and DamageSystem lag-comp).
    physicsWorld_ = std::make_unique<physics::PhysicsWorld>();

    // BW3: create and init mesh render system. MeshManager itself must be
    // constructed inside a beginFrame/endFrame pair; it is lazy-initialized in run().
    meshRenderSystem_ = std::make_unique<MeshRenderSystem>();
    meshRenderSystem_->init(engine_->device());

    context_.world           = &engine_->world();
    context_.systemScheduler = &scheduler_;
    context_.sceneManager    = &sceneManager_;

    // BW1: wire GameLoop with the PhysicsWorld so serverTick() calls step().
    GameLoop::Desc loopDesc{};
    loopDesc.scheduler    = &scheduler_;
    loopDesc.physicsWorld = physicsWorld_.get();
    gameLoop_.init(loopDesc);

    game_ = desc.game;

    // Load and wire start scene before onInit so IGame can find it in sceneManager.
    if (!desc.startScenePath.empty()) {
        if (core::scene::Scene* s = sceneManager_.load(desc.startScenePath)) {
            wireScene(*s);
            sceneManager_.activate(desc.startScenePath);
        }
    }

    if (game_) game_->onInit(context_);

    hostPort_    = desc.hostPort;
    connectAddr_ = desc.connectAddr;

    if (hostPort_ != 0) {
        LOG_INFO("Network mode: hosting on port %u", static_cast<unsigned>(hostPort_));
    } else if (!connectAddr_.empty()) {
        LOG_INFO("Network mode: connecting to %s", connectAddr_.c_str());
    }

    initialized_ = true;
    return true;
}

void Application::run() {
    using Clock   = std::chrono::high_resolution_clock;
    using Seconds = std::chrono::duration<float>;

    constexpr float kFixedDt      = 1.0f / 64.0f;
    constexpr float kMaxFrameTime = 0.25f;
    float           accumulator   = 0.0f;
    auto            prevTime      = Clock::now();

    engine_->run([&](core::ecs::World& /*world*/, rendering::FrameGraph& /*fg*/) {
        // BW3: MeshManager must be constructed while a frame is open (after beginFrame).
        // Engine::run() calls beginFrame() before this callback.
        if (!meshManager_) {
            meshManager_ = std::make_unique<rendering::MeshManager>(engine_->device());
        }

        // BW2: upload any meshes queued by the mesh-load delegate during scene activation.
        if (!pendingMeshLoads_.empty()) {
            // Resolve scene world once for the whole batch.
            core::scene::Scene* activeScene = sceneManager_.getActive();
            for (const auto& load : pendingMeshLoads_) {
                auto cpuMesh = tools::loadEasset(load.assetPath);
                if (cpuMesh) {
                    auto gpuHandle = meshManager_->uploadStatic(
                        std::span<const rendering::VertexStatic>(cpuMesh->vertices),
                        std::span<const uint32_t>(cpuMesh->indices));
                    meshRenderSystem_->registerHandle(load.entityIndex, gpuHandle);

                    // Auto-attach ColliderComponent if the .easset has a collision
                    // section and the entity does not already have one.
                    if (cpuMesh->collision && activeScene) {
                        core::ecs::Entity e{ load.entityIndex, load.entityGeneration };
                        auto& world = activeScene->world();
                        if (!world.tryGet<core::ColliderComponent>(e)) {
                            core::ColliderComponent cc{};
                            if (cpuMesh->collision->type ==
                                tools::CollisionType::ConvexHull) {
                                cc.shape = core::ColliderComponent::Shape::ConvexHull;
                            } else {
                                cc.shape = core::ColliderComponent::Shape::TriangleMesh;
                            }
                            world.addComponent<core::ColliderComponent>(e, cc);
                        }
                    }
                } else {
                    LOG_WARN("Application: failed to load mesh asset '{}'", load.assetPath);
                }
            }
            pendingMeshLoads_.clear();
        }

        auto  now     = Clock::now();
        float frameDt = std::min(Seconds(now - prevTime).count(), kMaxFrameTime);
        prevTime      = now;

        accumulator += frameDt;

        while (accumulator >= kFixedDt) {
            // Tick scene lifetime, world transforms, and dynamic grid (not physics —
            // GameLoop::serverTick() drives physics via the physicsWorld_ it was given).
            sceneManager_.tickActive(kFixedDt);
            gameLoop_.serverTick(context_, kFixedDt);
            if (game_) game_->onGameTick(context_, kFixedDt);
            accumulator -= kFixedDt;
        }

        scheduler_.tickGroup(TickGroup::Network, frameDt);
        scheduler_.tickGroup(TickGroup::Render,  frameDt);
        if (game_) game_->onRenderTick(context_, frameDt);

#ifdef ENGINE_DEVREL
        if (game_) game_->onDebugUI(context_);
#endif
    });
}

void Application::wireScene(core::scene::Scene& scene) {
    // BW1: store physicsWorld pointer on the scene for DamageSystem lag-comp rewind.
    // Physics stepping is handled by GameLoop::serverTick() (via loopDesc.physicsWorld),
    // not by Scene::tick(). Setting physicsStepFn here would double-step physics.
    scene.setPhysicsWorld(physicsWorld_.get());

    // BW2: queue mesh load requests; actual GPU upload runs inside the render frame.
    scene.setMeshLoadFn([this](core::ecs::Entity e, const std::string& path) {
        pendingMeshLoads_.push_back({e.index, e.generation, path});
    });
    scene.setMeshUnloadFn([this]() {
        if (meshRenderSystem_) meshRenderSystem_->clear();
        pendingMeshLoads_.clear();
    });
}

void Application::shutdown() {
    if (!initialized_) return;
    if (game_) game_->onShutdown(context_);
    engine_->shutdown();
    initialized_ = false;
}

} // namespace engine::app
