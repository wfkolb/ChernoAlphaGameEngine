#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace engine::core::memory {

    // Fixed-size pool allocator backed by an external buffer.
    // Maintains a singly-linked free list of slot indices stored inside
    // the free slots themselves (intrusive), so no separate list storage
    // is required when slots are in use.
    class PoolAllocator {
    public:
        // `buffer` must be at least elementSize * elementCount bytes and
        // aligned to at least alignof(uint32_t).
        // elementSize must be >= sizeof(uint32_t).
        PoolAllocator(void* buffer, uint32_t elementSize, uint32_t elementCount) noexcept;

        PoolAllocator(const PoolAllocator&)            = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;
        PoolAllocator(PoolAllocator&&)                 = delete;
        PoolAllocator& operator=(PoolAllocator&&)      = delete;

        // Pops a slot from the free list. Returns nullptr when exhausted.
        void* allocate() noexcept;

        // Returns `ptr` to the free list. Behaviour is undefined if `ptr`
        // was not obtained from this pool or has already been freed.
        void deallocate(void* ptr) noexcept;

        uint32_t freeCount()  const noexcept { return freeCount_;  }
        uint32_t totalCount() const noexcept { return elementCount_; }
        bool     isEmpty()    const noexcept { return freeCount_ == 0; }

    private:
        static constexpr uint32_t kEndOfList = 0xFFFF'FFFFu;

        uint8_t*  storage_;
        uint32_t  elementSize_;
        uint32_t  elementCount_;
        uint32_t  freeCount_;
        uint32_t  freeHead_;    // index of first free slot, kEndOfList when empty
    };

    // Typed wrapper around PoolAllocator that calls constructors and destructors.
    template<typename T>
    class TypedPool {
    public:
        // `buffer` must hold at least `count` elements of type T.
        TypedPool(void* buffer, uint32_t count) noexcept
            : pool_(buffer, static_cast<uint32_t>(sizeof(T)), count) {}

        template<typename... Args>
        T* construct(Args&&... args) {
            void* mem = pool_.allocate();
            if (!mem) return nullptr;
            return ::new (mem) T(std::forward<Args>(args)...);
        }

        void destroy(T* ptr) noexcept {
            if (!ptr) return;
            ptr->~T();
            pool_.deallocate(ptr);
        }

        uint32_t freeCount()  const noexcept { return pool_.freeCount();  }
        uint32_t totalCount() const noexcept { return pool_.totalCount(); }
        bool     isEmpty()    const noexcept { return pool_.isEmpty();     }

    private:
        PoolAllocator pool_;
    };

} // namespace engine::core::memory
