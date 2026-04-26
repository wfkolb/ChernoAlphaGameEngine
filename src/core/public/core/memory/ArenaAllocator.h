#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace engine::core::memory {

    class ArenaAllocator {
    public:
        // External-buffer constructor — the arena does NOT own the memory.
        ArenaAllocator(void* buffer, std::size_t capacity) noexcept;

        // Heap-backed constructor — the arena owns the allocation.
        explicit ArenaAllocator(std::size_t capacity);

        ~ArenaAllocator();

        ArenaAllocator(const ArenaAllocator&)            = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        ArenaAllocator(ArenaAllocator&&)                 = delete;
        ArenaAllocator& operator=(ArenaAllocator&&)      = delete;

        // Returns a pointer to at least `size` bytes aligned to `alignment`.
        // Returns nullptr if there is insufficient space.
        void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept;

        // Resets the cursor to the base; all previously allocated memory is invalidated.
        void reset() noexcept;

        std::size_t bytesUsed()      const noexcept;
        std::size_t bytesRemaining() const noexcept;
        std::size_t capacity()       const noexcept;

    private:
        uint8_t*    base_     { nullptr };
        uint8_t*    current_  { nullptr };
        uint8_t*    end_      { nullptr };
        bool        owned_    { false };

        friend class ScopedArena;
    };

    // RAII scope guard that snapshots the arena cursor on construction and
    // restores it on destruction, reclaiming all temporary allocations made
    // inside the scope without invalidating earlier allocations.
    class ScopedArena {
    public:
        explicit ScopedArena(ArenaAllocator& arena) noexcept;
        ~ScopedArena();

        ScopedArena(const ScopedArena&)            = delete;
        ScopedArena& operator=(const ScopedArena&) = delete;
        ScopedArena(ScopedArena&&)                 = delete;
        ScopedArena& operator=(ScopedArena&&)      = delete;

    private:
        ArenaAllocator& arena_;
        uint8_t*        savedCursor_;
    };

    // Convenience helper — constructs T in the arena and returns a pointer.
    template<typename T, typename... Args>
    T* emplace(ArenaAllocator& arena, Args&&... args) {
        void* mem = arena.allocate(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return ::new (mem) T(std::forward<Args>(args)...);
    }

} // namespace engine::core::memory
