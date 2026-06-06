#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "app/Engine.h"
#include "tools/Logger.h"
#include "tools/Config.h"
#include "networking/WinsockGuard.h"
#include "core/Input.h"
#include "core/log.h"
#include <core/components/ColliderComponent.h>
#include <core/components/AnimationState.h>
#include <core/components/MeshHandle.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/ecs/PrefabInstance.h>
#include <core/ecs/Name.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/components/TeamTag.h>
#include <core/input/InputReceiverComponent.h>
#include <physics/RigidBody.h>
#include <physics/CharacterController.h>
#include <networking/NetworkIdentity.h>

namespace engine::app {

struct Engine::WinsockGuardHolder {
    engine::networking::WinsockGuard guard;
};

Engine::Engine() = default;

Engine::~Engine() {
    if (initialized_) shutdown();
}

bool Engine::init(const EngineConfig& cfg) {
    // 1. Config
    engine::tools::Config::init();

    // 2. Logger
    engine::tools::Logger::init();
    LOG_INFO("Engine init begin");

    // 3. Winsock
    winsockGuard_ = std::make_unique<WinsockGuardHolder>();

    // 4. ECS + EventBus — register components in ID order before creating any world
    core::ecs::World::registerComponent<core::ecs::Name>({        // id 0
        "Name", sizeof(core::ecs::Name), alignof(core::ecs::Name),
        [](void* ptr) { new(ptr) core::ecs::Name{}; }, nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::Transform>({        // id 1
        "Transform", sizeof(core::Transform), alignof(core::Transform),
        [](void* ptr) { new(ptr) core::Transform{}; }, nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::input::InputReceiverComponent>({ // id 2
        "InputReceiverComponent",
        sizeof(core::input::InputReceiverComponent),
        alignof(core::input::InputReceiverComponent),
        [](void* ptr) { new(ptr) core::input::InputReceiverComponent{}; },
        nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::Health>({           // id 3
        "Health", sizeof(core::Health), alignof(core::Health),
        [](void* ptr) { new(ptr) core::Health{}; }, nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::Lifetime>({         // id 4
        "Lifetime", sizeof(core::Lifetime), alignof(core::Lifetime),
        [](void* ptr) { new(ptr) core::Lifetime{}; }, nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::TeamTag>({          // id 5
        "TeamTag", sizeof(core::TeamTag), alignof(core::TeamTag),
        [](void* ptr) { new(ptr) core::TeamTag{}; }, nullptr, nullptr
    });
    core::ecs::World::registerComponent<physics::RigidBody>({     // id 6
        "RigidBody", sizeof(physics::RigidBody), alignof(physics::RigidBody),
        [](void* ptr) { new(ptr) physics::RigidBody{}; }, nullptr, nullptr
    });
    core::ecs::World::registerComponent<physics::CharacterController>({ // id 7
        "CharacterController",
        sizeof(physics::CharacterController),
        alignof(physics::CharacterController),
        [](void* ptr) { new(ptr) physics::CharacterController{}; },
        nullptr, nullptr
    });
    core::ecs::World::registerComponent<networking::NetworkIdentity>({ // id 8
        "NetworkIdentity",
        sizeof(networking::NetworkIdentity),
        alignof(networking::NetworkIdentity),
        [](void* ptr) { new(ptr) networking::NetworkIdentity{}; },
        nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::ColliderComponent>({   // id 9
        "ColliderComponent",
        sizeof(core::ColliderComponent),
        alignof(core::ColliderComponent),
        [](void* ptr) { new(ptr) core::ColliderComponent{}; },
        nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::AnimationState>({      // id 10
        "AnimationState",
        sizeof(core::AnimationState),
        alignof(core::AnimationState),
        [](void* ptr) { new(ptr) core::AnimationState{}; },
        nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::ecs::HierarchyComponent>({ // id 11
        "HierarchyComponent",
        sizeof(core::ecs::HierarchyComponent),
        alignof(core::ecs::HierarchyComponent),
        [](void* ptr) { new(ptr) core::ecs::HierarchyComponent{}; },
        nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::MeshHandle>({          // id 12
        "MeshHandle",
        sizeof(core::MeshHandle),
        alignof(core::MeshHandle),
        [](void* ptr) { new(ptr) core::MeshHandle{}; },
        nullptr, nullptr
    });
    core::ecs::World::registerComponent<core::ecs::PrefabInstance>({ // id 13
        "PrefabInstance",
        sizeof(core::ecs::PrefabInstance),
        alignof(core::ecs::PrefabInstance),
        [](void* ptr) { new(ptr) core::ecs::PrefabInstance{}; },
        nullptr, nullptr
    });
    world_    = std::make_unique<core::ecs::World>();
    eventBus_ = std::make_unique<core::EventBus>();

    // 5. Window
    rendering::Window::Desc wdesc;
    wdesc.width  = cfg.windowWidth;
    wdesc.height = cfg.windowHeight;
    wdesc.title  = cfg.windowTitle;
    window_ = std::make_unique<WindowHolder>(WindowHolder{ rendering::Window::create(wdesc) });

    // 6. GpuDevice
    rendering::GpuDevice::Desc ddesc;
    ddesc.window = &win();
    ddesc.vsync  = cfg.vsync;
    device_ = std::make_unique<GpuDeviceHolder>(GpuDeviceHolder{ rendering::GpuDevice::create(ddesc) });

    // 7. FrameGraph
    frameGraph_ = std::make_unique<rendering::FrameGraph>();

    // 8. RawInput
    core::InputSystem::registerRawInput(win().nativeHandle());

    initialized_ = true;
    LOG_INFO("Engine init complete ({} x {})", cfg.windowWidth, cfg.windowHeight);
    return true;
}

void Engine::run(std::function<void(core::ecs::World&, rendering::FrameGraph&)> onTick) {
    while (!win().wantsClose()) {
        // Drain the Win32 message queue.
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_INPUT) {
                core::InputSystem::processRawInput(reinterpret_cast<void*>(msg.lParam));
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        core::InputSystem::update();

        dev().beginFrame();
        frameGraph_->reset();

        if (onTick) onTick(*world_, *frameGraph_);

        frameGraph_->compile();
        frameGraph_->execute(dev().nativeCommandList());

        dev().endFrame();
    }
}

void Engine::shutdown() {
    if (!initialized_) return;
    LOG_INFO("Engine shutdown");
    dev().flush();
    frameGraph_.reset();
    device_.reset();
    window_.reset();
    world_.reset();
    eventBus_.reset();
    winsockGuard_.reset();
    engine::tools::Logger::shutdown();
    engine::tools::Config::shutdown();
    initialized_ = false;
}

} // namespace engine::app
