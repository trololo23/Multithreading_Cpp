#pragma once

#include <utility>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>

template <class T>
class BufferedChannel {
public:
    explicit BufferedChannel(int size) : capacity_(size){};

    void Send(const T& value) {
        std::unique_lock guard(mutex_);
        if (stoped_) {
            throw std::runtime_error("was closed");
        }
        not_full_.wait(guard, [this] { return stoped_ || buffer_.size() < capacity_; });
        if (stoped_) {
            throw std::runtime_error("was closed");
        }
        buffer_.push_back(value);
        not_empty_.notify_one();
    }

    std::optional<T> Recv() {
        std::unique_lock guard(mutex_);
        not_empty_.wait(guard, [this] { return stoped_ || !buffer_.empty(); });
        if (buffer_.empty()) {
            return std::nullopt;
        }
        T result = std::move(buffer_.front());
        buffer_.pop_front();
        not_full_.notify_one();
        return result;
    }

    void Close() {
        std::unique_lock guard(mutex_);
        stoped_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    size_t capacity_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::mutex mutex_;
    std::deque<T> buffer_;
    bool stoped_{false};
};
