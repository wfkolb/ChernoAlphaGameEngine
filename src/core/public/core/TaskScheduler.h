#pragma once

#include <core/diag/Assert.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace engine::core {

// Minimal fixed-size thread pool.
// Thread count is clamped to [2, 8] based on hardware concurrency.
// submit() enqueues a task and returns a std::future<void> that resolves
// when the task finishes.
// wait() blocks the calling thread until all currently-submitted tasks
// have completed.
// The destructor joins all worker threads cleanly (no tasks are dropped).
class TaskScheduler {
public:
    TaskScheduler();
    explicit TaskScheduler(unsigned int threadCount);
    ~TaskScheduler();

    ENGINE_NO_COPY(TaskScheduler);
    ENGINE_NO_MOVE(TaskScheduler);

    // Returns the number of worker threads.
    unsigned int threadCount() const noexcept { return static_cast<unsigned int>(workers_.size()); }

    // Enqueue a task. Returns a future that becomes ready when the task completes.
    std::future<void> submit(std::function<void()> task);

    // Block until all enqueued tasks have finished executing.
    void wait();

private:
    void workerLoop();

    std::vector<std::thread>         workers_;
    std::queue<std::packaged_task<void()>> queue_;
    std::mutex                       queueMutex_;
    std::condition_variable          cv_;
    std::atomic<bool>                stopping_{ false };

    // Track in-flight task count for wait().
    std::atomic<int>                 pending_{ 0 };
    std::mutex                       waitMutex_;
    std::condition_variable          waitCv_;
};

} // namespace engine::core
