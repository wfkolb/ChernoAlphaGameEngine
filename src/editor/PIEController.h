#pragma once
#ifdef ENGINE_DEVREL

#include <core/ecs/Entity.h>

#include <cstdint>
#include <vector>

namespace engine::core::scene { class Scene; }

namespace engine::editor {

// Play-in-Editor controller.
//
// On start(): snapshots the edited scene's world into an in-memory buffer
// (never to disk), then runs the scene's own simulation loop in-process —
// this acts as the local server+client for PIE. On stop(): tears the play
// state down and restores the editor world from the snapshot so edits made
// during play do not leak back into the authoring scene.
//
// Networking-driven prediction/replication is intentionally out of scope here:
// the editor module links only core/rendering/tools/physics, so PIE drives the
// Scene + PhysicsWorld directly rather than a networked Session.
class PIEController {
public:
    enum class State { Stopped, Playing, Paused };

    // Begin play. Captures a restore snapshot of `scene`'s world. No-op if
    // already playing.
    void start(core::scene::Scene& scene);

    // Advance the play simulation by dt (fixed 64 Hz steps internally).
    void tick(float dt);

    void pause();
    void resume();

    // End play and restore the editor world from the captured snapshot.
    void stop();

    State    state()       const noexcept { return state_; }
    bool     isPlaying()   const noexcept { return state_ != State::Stopped; }
    uint32_t simTick()     const noexcept { return simTick_; }
    float    playTime()    const noexcept { return playTime_; }

    void setPiePort(uint16_t port) noexcept { piePort_ = port; }

    // True while play is active: the viewport should use the player entity's
    // Transform for the view matrix and suppress editor-camera WASD input.
    bool isUsingPlayerCamera() const noexcept { return usePlayerCamera_; }
    bool isCapturingMouse()    const noexcept { return captureMouse_; }

private:
    struct ComponentBlob {
        core::ecs::ComponentTypeId typeId;
        std::vector<uint8_t>       bytes;
    };
    struct EntitySnapshot {
        core::ecs::Entity          entity;
        std::vector<ComponentBlob> components;
    };

    void captureSnapshot();
    void restoreSnapshot();

    core::scene::Scene*           scene_ = nullptr;
    State                         state_ = State::Stopped;
    uint16_t                      piePort_          = 57300;
    std::vector<EntitySnapshot>   snapshot_;
    float                         accumulator_      = 0.0f;
    float                         playTime_         = 0.0f;
    uint32_t                      simTick_          = 0;
    bool                          usePlayerCamera_  = false;
    bool                          captureMouse_     = false;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
