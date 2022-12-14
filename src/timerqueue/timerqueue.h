#pragma once

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <map>
#include <deque>
#include <queue>
#include <iostream>

template <class T>
class TimerQueue {
public:
    using Clock = std::chrono::system_clock;
    using TimePoint = Clock::time_point;

public:
    void Add(const T& item, TimePoint at) {
        {
            std::unique_lock guard(mutex_);
            buffer_.push({at, item});
        }

        not_empty_.notify_one();
    }

    T Pop() {
        std::unique_lock guard(mutex_);
        if (!buffer_.empty()) {
            auto time = buffer_.top().first;
            not_empty_.wait_until(guard, time);
        } else {
            not_empty_.wait(guard, [this] { return !buffer_.empty(); });
        }

        std::pair<TimePoint, T> pair = std::move(buffer_.top());
        buffer_.pop();
        return pair.second;
    }

private:
    std::mutex mutex_;
    std::condition_variable not_empty_;

    std::priority_queue<std::pair<TimePoint, T>, std::vector<std::pair<TimePoint, T>>,
                        std::greater<>>
        buffer_;
};
