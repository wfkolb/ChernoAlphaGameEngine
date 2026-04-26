#include "core/memory/PoolAllocator.h"

#include <cstring>

namespace engine::core::memory {

    PoolAllocator::PoolAllocator(void* buffer, uint32_t elementSize, uint32_t elementCount) noexcept
        : storage_     (static_cast<uint8_t*>(buffer))
        , elementSize_ (elementSize)
        , elementCount_(elementCount)
        , freeCount_   (elementCount)
        , freeHead_    (elementCount > 0 ? 0u : kEndOfList)
    {
        // Build the initial free list by writing the next-slot index into the
        // first sizeof(uint32_t) bytes of every slot, then terminate.
        for (uint32_t i = 0; i < elementCount_; ++i) {
            const uint32_t next = (i + 1 < elementCount_) ? (i + 1) : kEndOfList;
            std::memcpy(storage_ + static_cast<std::size_t>(i) * elementSize_, &next, sizeof(uint32_t));
        }
    }

    void* PoolAllocator::allocate() noexcept {
        if (freeHead_ == kEndOfList) return nullptr;

        uint8_t* const slot = storage_ + static_cast<std::size_t>(freeHead_) * elementSize_;

        uint32_t next{};
        std::memcpy(&next, slot, sizeof(uint32_t));
        freeHead_ = next;

        --freeCount_;
        return slot;
    }

    void PoolAllocator::deallocate(void* ptr) noexcept {
        if (!ptr) return;

        // Write the current head index into the slot's link word, then make
        // this slot the new head.
        uint8_t* const slot = static_cast<uint8_t*>(ptr);
        std::memcpy(slot, &freeHead_, sizeof(uint32_t));

        const uint32_t slotIndex = static_cast<uint32_t>(
            (slot - storage_) / static_cast<std::ptrdiff_t>(elementSize_));
        freeHead_ = slotIndex;

        ++freeCount_;
    }

} // namespace engine::core::memory
