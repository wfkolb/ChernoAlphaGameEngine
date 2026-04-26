#pragma once

#include <cstdint>

namespace engine::core::memory {

    template<typename T>
    struct Handle {
        uint32_t index      { 0xFFFF'FFFFu };
        uint32_t generation { 0 };

        bool isValid() const noexcept { return index != 0xFFFF'FFFFu; }
        bool operator==(const Handle&) const noexcept = default;
    };

    template<typename T>
    inline constexpr Handle<T> kInvalidHandle{};

} // namespace engine::core::memory
