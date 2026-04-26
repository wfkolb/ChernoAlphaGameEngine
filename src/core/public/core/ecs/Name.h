#pragma once

#include "core/ecs/Entity.h"

namespace engine::core::ecs {

struct Name {
    static constexpr size_t kMaxLen = 63;
    static constexpr ComponentTypeId kComponentId = 0;

    char buf[kMaxLen + 1] = {};

    Name() = default;
    explicit Name(const char* s) noexcept {
        strncpy_s(buf, sizeof(buf), s, kMaxLen);
    }
    const char* c_str() const noexcept { return buf; }
};

static_assert(sizeof(Name) == 64);
static_assert(alignof(Name) == 1);

} // namespace engine::core::ecs
