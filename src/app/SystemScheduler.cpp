#include "app/SystemScheduler.h"

namespace engine::app {

void SystemScheduler::registerSystem(TickGroup group, SystemFn fn) {
    groups_[static_cast<int>(group)].push_back(std::move(fn));
}

void SystemScheduler::tickGroup(TickGroup group, float dt) {
    for (auto& fn : groups_[static_cast<int>(group)])
        fn(dt);
}

} // namespace engine::app
