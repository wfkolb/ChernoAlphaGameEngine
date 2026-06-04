#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace engine::app {

enum class TickGroup : uint8_t {
    PrePhysics,
    PostPhysics,
    GameFixed,
    Render,
    Network,
    kCount_
};

using SystemFn = std::function<void(float /*dt*/)>;

class SystemScheduler {
public:
    void registerSystem(TickGroup group, SystemFn fn);
    void tickGroup(TickGroup group, float dt);

private:
    static constexpr int kGroupCount = static_cast<int>(TickGroup::kCount_);
    std::vector<SystemFn> groups_[kGroupCount];
};

} // namespace engine::app
