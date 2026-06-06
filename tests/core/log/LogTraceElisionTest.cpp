#include <gtest/gtest.h>
#include <core/log.h>

// Verify that LOG_TRACE is a strict no-op in Release builds (NDEBUG &&
// !ENGINE_DEVREL) by checking it produces no side effects at runtime.
TEST(LogTrace, NoSideEffectsInRelease) {
#if defined(NDEBUG) && !defined(ENGINE_DEVREL)
    int counter = 0;
    LOG_TRACE("this should be elided: {}", ++counter);
    EXPECT_EQ(counter, 0) << "LOG_TRACE evaluated its arguments in a Release build";
#else
    // In Debug / DevRel the macro IS active; just confirm it compiles and skip.
    GTEST_SKIP() << "LOG_TRACE elision only applies to Release (NDEBUG) builds";
#endif
}
