#pragma once

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace Threading {
    inline unsigned int defaultMaxThreads() {
        return std::max(1u, std::thread::hardware_concurrency());
    }
    inline std::atomic<unsigned int> maxThreads{defaultMaxThreads()};

    inline unsigned int getMaxThreads() {
        return maxThreads.load(std::memory_order_relaxed);
    }
    inline void setMaxThreads(unsigned int count) {
        maxThreads.store(std::max(1u, count), std::memory_order_relaxed);
    }

    // The caller participates, so the limit includes it. Exceptions are rethrown after all workers join.
    template <typename Function> void parallelFor(size_t count, Function&& function) {
        if (count == 0)
            return;
        const size_t workers = std::min<size_t>(count, getMaxThreads());
        if (workers == 1) {
            for (size_t i = 0; i < count; ++i)
                function(i);
            return;
        }
        std::atomic<size_t> next{0};
        std::atomic<bool> failed{false};
        std::exception_ptr error;
        std::mutex errorMutex;
        const auto run = [&] {
            try {
                while (!failed.load(std::memory_order_relaxed)) {
                    const size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= count)
                        break;
                    function(i);
                }
            } catch (...) {
                std::lock_guard lock(errorMutex);
                if (!error)
                    error = std::current_exception();
                failed.store(true, std::memory_order_relaxed);
            }
        };
        {
            std::vector<std::jthread> threads;
            threads.reserve(workers - 1);
            for (size_t i = 1; i < workers; ++i)
                threads.emplace_back(run);
            run();
        }
        if (error)
            std::rethrow_exception(error);
    }
} // namespace Threading
