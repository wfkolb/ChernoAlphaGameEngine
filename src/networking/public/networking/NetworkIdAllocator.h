#pragma once
#include <cstdint>
#include <vector>

namespace engine::networking {

// Monotonic uint32 allocator. Net ID 0 is reserved as invalid.
// Released IDs are recycled LIFO to minimise re-use delay.
class NetworkIdAllocator {
public:
    static constexpr uint32_t kInvalid = 0u;

    uint32_t allocate();
    void     release(uint32_t id);
    void     reset()  noexcept;

    uint32_t nextMonotonic() const noexcept { return next_; }

private:
    uint32_t              next_ = 1u;
    std::vector<uint32_t> free_;
};

} // namespace engine::networking
