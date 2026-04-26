#include "core/diag/Assert.h"

#include <cstdio>

namespace engine::core::diag {

    void reportAssertionFailure(const char* file, int line, const char* expr, const char* message) noexcept {
        std::fprintf(stderr,
                     "ENGINE_ASSERT failed: %s\n  at %s:%d\n  message: %s\n",
                     expr ? expr : "<null>",
                     file ? file : "<null>",
                     line,
                     (message && message[0]) ? message : "<none>");
        std::fflush(stderr);
    }

}
