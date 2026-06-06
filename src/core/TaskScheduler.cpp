#include "core/TaskScheduler.h"

#include <algorithm>

namespace engine::core {

static unsigned int clampThreadCount(unsigned int n) noexcept {
    constexpr unsigned int kMin = 2u;
    constexpr unsigned int kMax = 8u;
    return std::max(kMin, std::min(kMax, n));
}

TaskScheduler::TaskScheduler()
    : TaskScheduler(std::thread::hardware_concurrency())
{}

TaskScheduler::TaskScheduler(unsigned int threadCount) {
    const unsigned int count = clampThreadCount(threadCount);
    workers_.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

TaskScheduler::~TaskScheduler() {
    stopping_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
    for (std::thread& t : workers_) {
        if (t.joinable()) t.join();
    }
}

std::future<void> TaskScheduler::submit(std::function<void()> task) {
    ENGINE_ASSERT(task, "TaskScheduler::submit: null task");

    // Wrap in a packaged_task so we can return a future.
    // pending_ is incremented here (before the task runs) so wait() is safe.
    pending_.fetch_add(1, std::memory_order_relaxed);

    std::packaged_task<void()> pt([fn = std::move(task), this]() mutable {
        fn();
        // Decrement pending and wake any thread blocked in wait().
        if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lk(waitMutex_);
            waitCv_.notify_all();
        }
    });

    std::future<void> fut = pt.get_future();
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        queue_.push(std::move(pt));
    }
    cv_.notify_one();
    return fut;
}

void TaskScheduler::wait() {
    std::unique_lock<std::mutex> lk(waitMutex_);
    waitCv_.wait(lk, [this] {
        return pending_.load(std::memory_order_acquire) == 0;
    });
}

void TaskScheduler::workerLoop() {
    while (true) {
        std::packaged_task<void()> task;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            cv_.wait(lk, [this] {
                return stopping_.load(std::memory_order_relaxed) || !queue_.empty();
            });
            if (queue_.empty()) {
                // stopping_ is true and queue is drained — exit.
                break;
            }
            task = std::move(queue_.front());
            queue_.pop();
        }
        task();
    }
}

} // namespace engine::core
