#include <gtest/gtest.h>
#include <core/TaskScheduler.h>

#include <atomic>
#include <chrono>
#include <numeric>
#include <vector>

using engine::core::TaskScheduler;

// ── Basic execution ──────────────────────────────────────────────────────────

TEST(TaskScheduler, TaskRuns) {
    TaskScheduler sched;
    std::atomic<bool> ran{ false };
    auto fut = sched.submit([&ran] { ran.store(true, std::memory_order_relaxed); });
    fut.get();
    EXPECT_TRUE(ran.load());
}

TEST(TaskScheduler, FutureBecomesReadyAfterTask) {
    TaskScheduler sched;
    std::atomic<int> value{ 0 };
    auto fut = sched.submit([&value] { value.store(42, std::memory_order_relaxed); });
    fut.get();
    EXPECT_EQ(value.load(), 42);
}

// ── Wait blocks until all tasks are done ────────────────────────────────────

TEST(TaskScheduler, WaitBlocksUntilAllDone) {
    TaskScheduler sched;
    constexpr int kTasks = 64;
    std::atomic<int> counter{ 0 };

    for (int i = 0; i < kTasks; ++i) {
        sched.submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    sched.wait();
    EXPECT_EQ(counter.load(), kTasks);
}

// ── Parallel tasks produce correct results ───────────────────────────────────

TEST(TaskScheduler, ParallelSumIsCorrect) {
    TaskScheduler sched;
    constexpr int    kSize  = 1024;
    constexpr int    kChunk = 64;

    // Fill input with 1..kSize.
    std::vector<int> data(kSize);
    std::iota(data.begin(), data.end(), 1);

    // Accumulate partial sums in per-chunk storage.
    const int numChunks = kSize / kChunk;
    std::vector<int> partials(static_cast<size_t>(numChunks), 0);

    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(numChunks));

    for (int c = 0; c < numChunks; ++c) {
        futures.push_back(sched.submit([&data, &partials, c, kChunk] {
            int sum = 0;
            for (int i = c * kChunk; i < (c + 1) * kChunk; ++i)
                sum += data[static_cast<size_t>(i)];
            partials[static_cast<size_t>(c)] = sum;
        }));
    }

    sched.wait();

    const int total    = std::accumulate(partials.begin(), partials.end(), 0);
    const int expected = kSize * (kSize + 1) / 2; // sum of 1..kSize
    EXPECT_EQ(total, expected);
}

// ── Destructor does not deadlock ─────────────────────────────────────────────

TEST(TaskScheduler, DestructorDoesNotDeadlock) {
    // The destructor must join all threads even when there are pending tasks.
    {
        TaskScheduler sched;
        for (int i = 0; i < 32; ++i) {
            sched.submit([] {
                // simulate a small amount of work
                volatile int x = 0;
                for (int j = 0; j < 1000; ++j) x += j;
                (void)x;
            });
        }
        // Destructor runs here — must not deadlock.
    }
    SUCCEED();
}

// ── Thread count is clamped ───────────────────────────────────────────────────

TEST(TaskScheduler, ThreadCountIsAtLeastTwo) {
    TaskScheduler sched;
    EXPECT_GE(sched.threadCount(), 2u);
}

TEST(TaskScheduler, ThreadCountIsAtMostEight) {
    TaskScheduler sched;
    EXPECT_LE(sched.threadCount(), 8u);
}

TEST(TaskScheduler, CustomThreadCountClamped) {
    // Request more than the max — should be clamped to 8.
    TaskScheduler sched(32u);
    EXPECT_EQ(sched.threadCount(), 8u);
}

TEST(TaskScheduler, CustomThreadCountMin) {
    // Request 0 — should be clamped to 2.
    TaskScheduler sched(0u);
    EXPECT_EQ(sched.threadCount(), 2u);
}

// ── Wait on empty queue returns immediately ──────────────────────────────────

TEST(TaskScheduler, WaitWithNoTasksReturns) {
    TaskScheduler sched;
    // Should not block.
    sched.wait();
    SUCCEED();
}

// ── Multiple sequential wait() calls are safe ───────────────────────────────

TEST(TaskScheduler, MultipleWaitsAreSafe) {
    TaskScheduler sched;

    for (int round = 0; round < 4; ++round) {
        std::atomic<int> counter{ 0 };
        for (int i = 0; i < 16; ++i) {
            sched.submit([&counter] {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        sched.wait();
        EXPECT_EQ(counter.load(), 16);
    }
}
