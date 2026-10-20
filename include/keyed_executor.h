#ifndef KEYED_EXECUTOR_H
#define KEYED_EXECUTOR_H

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Different sessions run concurrently. A session's accepted jobs run in FIFO
// order without occupying a worker while waiting for another job in that session.
class KeyedExecutor {
public:
    enum class Admission { accepted, full, session_full, stopped };

    KeyedExecutor(std::size_t workers, std::size_t capacity, std::size_t per_session)
        : capacity_(capacity), per_session_(per_session) {
        if (!workers || !capacity || !per_session) {
            throw std::invalid_argument("Executor limits must be positive");
        }
        try {
            for (std::size_t i = 0; i < workers; ++i) {
                workers_.emplace_back([this] { run(); });
            }
        } catch (...) {
            stop();
            throw;
        }
    }
    KeyedExecutor(const KeyedExecutor&) = delete;
    KeyedExecutor& operator=(const KeyedExecutor&) = delete;
    ~KeyedExecutor() { stop(); }

    Admission submit(std::string key, std::function<void()> work) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return Admission::stopped;
        if (outstanding_ >= capacity_) return Admission::full;
        const auto count = counts_.find(key);
        if (count != counts_.end() && count->second >= per_session_) {
            return Admission::session_full;
        }
        // Roll back the count if queue allocation fails.
        auto entry = counts_.try_emplace(key, 0).first;
        try {
            queue_.push_back({std::move(key), std::move(work)});
        } catch (...) {
            if (!entry->second) counts_.erase(entry);
            throw;
        }
        ++entry->second;
        ++outstanding_;
        ready_.notify_all();
        return Admission::accepted;
    }

    // Cancel queued work and finish the at most `workers` in-flight jobs.
    // Call after producers have stopped; only one caller may join at a time.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            for (const auto& job : queue_) {
                if (--counts_.at(job.key) == 0) counts_.erase(job.key);
                --outstanding_;
            }
            queue_.clear();
        }
        ready_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

private:
    struct Job { std::string key; std::function<void()> work; };
    auto runnable() {
        return std::find_if(queue_.begin(), queue_.end(), [this](const Job& job) {
            return active_.find(job.key) == active_.end();
        });
    }
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || runnable() != queue_.end(); });
                if (stopping_) return;
                auto next = runnable();
                active_.insert(next->key);
                job = std::move(*next);
                queue_.erase(next);
            }
            // Application jobs log/report exceptions. Never strand a session.
            try { job.work(); } catch (...) {}
            {
                std::lock_guard<std::mutex> lock(mutex_);
                active_.erase(job.key);
                if (--counts_.at(job.key) == 0) counts_.erase(job.key);
                --outstanding_;
            }
            ready_.notify_all();
        }
    }
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Job> queue_;
    std::set<std::string> active_;
    std::map<std::string, std::size_t> counts_;
    std::vector<std::thread> workers_;
    std::size_t capacity_, per_session_, outstanding_ = 0;
    bool stopping_ = false;
};
#endif
