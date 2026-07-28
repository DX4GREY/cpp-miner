#pragma once
//
// ThreadPool.hpp
// General-purpose fixed-size thread pool for short discrete tasks (used
// by the application for periodic housekeeping such as stats printing).
//
// Note: the mining workers themselves are NOT scheduled through this
// pool -- each mining worker is a long-lived thread that continuously
// hashes against the current job (see MinerEngine), which is a better
// fit than a task queue for a workload with no natural "task boundary".
// This ThreadPool exists as a reusable primitive for the rest of the
// application (e.g. background/periodic work) and to keep threading
// concerns cleanly separated from mining logic.
//

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

namespace cppminer::threading {

/// Fixed-size pool of worker threads consuming a shared task queue.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Enqueues a task and returns a future for its result.
    template <typename F, typename R = std::invoke_result_t<F>>
    std::future<R> submit(F&& task) {
        auto packaged = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
        std::future<R> future = packaged->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.emplace([packaged]() { (*packaged)(); });
        }
        condition_.notify_one();
        return future;
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopping_{false};
};

} // namespace cppminer::threading
