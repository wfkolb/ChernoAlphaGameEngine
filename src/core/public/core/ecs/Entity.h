#pragma once

#include <cstdint>
#include <bitset>
#include <cstddef>

namespace engine::core::ecs {

    struct Entity {
        uint32_t index      = ~0u;
        uint32_t generation = 0;
        bool operator==(const Entity&) const noexcept = default;
    };

    inline constexpr Entity kInvalidEntity{ ~0u, 0 };

    using ComponentTypeId = uint8_t;
    using ComponentMask   = std::bitset<256>;

    struct EditorContext;

    struct ComponentMeta {
        const char* name    = nullptr;
        size_t      size    = 0;
        size_t      align   = 0;
        void (*construct)(void* ptr)                            = nullptr;
        void (*destruct) (void* ptr)                            = nullptr;
        void (*inspect)  (void* ptr, EditorContext&)            = nullptr;
    };

} // namespace engine::core::ecs
