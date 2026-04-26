#pragma once
#include <functional>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <cstdint>

namespace engine::core {

// Synchronous publish/subscribe event bus.
// Events are any copyable struct. No heap allocation per dispatch.
// Subscribe returns a ListenerHandle; unsubscribe via removeListener(handle).

class EventBus {
public:
    using ListenerHandle = uint64_t;

    template<typename EventT>
    ListenerHandle subscribe(std::function<void(const EventT&)> handler);

    void removeListener(ListenerHandle handle);

    template<typename EventT>
    void publish(const EventT& event);

private:
    struct ListenerEntry {
        ListenerHandle       handle;
        std::type_index      eventType;
        std::function<void(const void*)> wrapper;  // type-erased
    };

    std::vector<ListenerEntry> listeners_;
    uint64_t                   nextHandle_ = 1;
};

// Template implementations:
template<typename EventT>
EventBus::ListenerHandle EventBus::subscribe(std::function<void(const EventT&)> handler) {
    uint64_t h = nextHandle_++;
    listeners_.push_back({
        h,
        std::type_index(typeid(EventT)),
        [handler = std::move(handler)](const void* ev) {
            handler(*static_cast<const EventT*>(ev));
        }
    });
    return h;
}

template<typename EventT>
void EventBus::publish(const EventT& event) {
    const std::type_index typeIdx(typeid(EventT));
    for (auto& entry : listeners_) {
        if (entry.eventType == typeIdx) {
            entry.wrapper(static_cast<const void*>(&event));
        }
    }
}

} // namespace engine::core
