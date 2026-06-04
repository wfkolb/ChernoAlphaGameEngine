#include <networking/NetworkIdAllocator.h>

namespace engine::networking {

uint32_t NetworkIdAllocator::allocate() {
    if (!free_.empty()) {
        const uint32_t id = free_.back();
        free_.pop_back();
        return id;
    }
    return next_++;
}

void NetworkIdAllocator::release(uint32_t id) {
    if (id != kInvalid) {
        free_.push_back(id);
    }
}

void NetworkIdAllocator::reset() noexcept {
    next_ = 1u;
    free_.clear();
}

} // namespace engine::networking
