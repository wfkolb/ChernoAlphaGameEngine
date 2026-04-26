#pragma once

#include <cstdio>
#include <cstdlib>

namespace engine::core::diag {

    void reportAssertionFailure(const char* file, int line, const char* expr, const char* message) noexcept;

}

#if defined(_MSC_VER)
    #define ENGINE_DEBUG_BREAK() __debugbreak()
#else
    #define ENGINE_DEBUG_BREAK() ((void)0)
#endif

#if !defined(NDEBUG)
    #define ENGINE_ASSERT(cond, ...)                                                              \
        do {                                                                                      \
            if (!(cond)) {                                                                        \
                ::engine::core::diag::reportAssertionFailure(__FILE__, __LINE__, #cond, "" __VA_ARGS__); \
                ENGINE_DEBUG_BREAK();                                                             \
                std::abort();                                                                     \
            }                                                                                     \
        } while (0)
#else
    #define ENGINE_ASSERT(cond, ...) ((void)0)
#endif

#define ENGINE_VERIFY(cond, ...)                                                                  \
    do {                                                                                          \
        if (!(cond)) {                                                                            \
            ::engine::core::diag::reportAssertionFailure(__FILE__, __LINE__, #cond, "" __VA_ARGS__); \
            std::abort();                                                                         \
        }                                                                                         \
    } while (0)

#define ENGINE_NO_COPY(Type)                                                                      \
    Type(const Type&) = delete;                                                                   \
    Type& operator=(const Type&) = delete

#define ENGINE_NO_MOVE(Type)                                                                      \
    Type(Type&&) = delete;                                                                        \
    Type& operator=(Type&&) = delete
