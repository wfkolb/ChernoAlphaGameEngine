#include <gtest/gtest.h>
#include <core/time/Clock.h>
#include <thread>
#include <chrono>

using namespace engine::core::time;

TEST(ClockTests, Monotonic) {
    const uint64_t t0 = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const uint64_t t1 = Clock::now();
    EXPECT_GT(t1, t0);
}

TEST(ClockTests, ResolutionAtLeast1us) {
    // ticksPerSecond >= 1,000,000 implies resolution <= 1 µs
    EXPECT_GE(Clock::ticksPerSecond(), 1'000'000u);
}

TEST(ClockTests, ToSecondsReasonable) {
    const uint64_t t0 = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const uint64_t t1 = Clock::now();
    const double elapsed = Clock::toSeconds(t1 - t0);
    EXPECT_GT(elapsed, 0.005);
    EXPECT_LT(elapsed, 0.5);
}
