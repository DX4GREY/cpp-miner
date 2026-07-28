#include "threading/ThreadPool.hpp"

namespace cppminer::threading {

ThreadPool::ThreadPool(std::size_t threadCount) {
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [this]() { return stopping_.load() || !tasks_.empty(); });
                    if (stopping_.load() && tasks_.empty()) {
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stopping_.store(true);
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

} // namespace cppminer::threading
