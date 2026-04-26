#include "core/memory/ArenaAllocator.h"

#include <cstdlib>

namespace engine::core::memory {

    ArenaAllocator::ArenaAllocator(void* buffer, std::size_t capacity) noexcept
        : base_    (static_cast<uint8_t*>(buffer))
        , current_ (static_cast<uint8_t*>(buffer))
        , end_     (static_cast<uint8_t*>(buffer) + capacity)
        , owned_   (false)
    {}

    ArenaAllocator::ArenaAllocator(std::size_t capacity)
        : base_    (static_cast<uint8_t*>(std::malloc(capacity)))
        , current_ (nullptr)
        , end_     (nullptr)
        , owned_   (true)
    {
        // Set cursor and end only on successful allocation.
        // Callers must check allocate() return value on OOM.
        if (base_) {
            current_ = base_;
            end_     = base_ + capacity;
        }
    }

    ArenaAllocator::~ArenaAllocator() {
        if (owned_) {
            std::free(base_);
        }
    }

    void* ArenaAllocator::allocate(std::size_t size, std::size_t alignment) noexcept {
        if (!current_ || size == 0) return nullptr;

        // Align current_ up to the requested alignment.
        const uintptr_t raw     = reinterpret_cast<uintptr_t>(current_);
        const uintptr_t aligned = (raw + alignment - 1u) & ~(alignment - 1u);
        uint8_t* const  start   = reinterpret_cast<uint8_t*>(aligned);

        if (start + size > end_) return nullptr;

        current_ = start + size;
        return start;
    }

    void ArenaAllocator::reset() noexcept {
        current_ = base_;
    }

    std::size_t ArenaAllocator::bytesUsed() const noexcept {
        if (!base_) return 0u;
        return static_cast<std::size_t>(current_ - base_);
    }

    std::size_t ArenaAllocator::bytesRemaining() const noexcept {
        if (!current_) return 0u;
        return static_cast<std::size_t>(end_ - current_);
    }

    std::size_t ArenaAllocator::capacity() const noexcept {
        if (!base_) return 0u;
        return static_cast<std::size_t>(end_ - base_);
    }

    // -------------------------------------------------------------------------
    // ScopedArena
    // -------------------------------------------------------------------------

    ScopedArena::ScopedArena(ArenaAllocator& arena) noexcept
        : arena_       (arena)
        , savedCursor_ (arena.current_)
    {}

    ScopedArena::~ScopedArena() {
        arena_.current_ = savedCursor_;
    }

} // namespace engine::core::memory
