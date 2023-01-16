#pragma once

#include <utility>
#include <optional>
#include <condition_variable>
#include <iostream>
#include <deque>
template <class T>
class UnbufferedChannel {
public:
    void Send(const T& value) {
        std::unique_lock<std::mutex> the_lock(mutex_);
        send_.wait(the_lock, [this] { return stoped_ || (has_receiver_ && !has_value_); });
        if (stoped_) {
            throw std::runtime_error("was closed");
        }
        val_ = value;
        has_value_ = true;
        recv_.notify_one();
    }

    std::optional<T> Recv() {
        std::unique_lock<std::mutex> the_lock(mutex_);
        has_receiver_ = true;
        send_.notify_one();
        recv_.wait(the_lock, [this] { return stoped_ || (has_receiver_ && has_value_); });
        if (stoped_ && !has_value_) {
            return std::nullopt;
        }
        has_value_ = false;
        has_receiver_ = false;
        return std::move(val_);
    }

    void Close() {
        std::unique_lock guard(mutex_);
        stoped_ = true;
        send_.notify_all();
        recv_.notify_all();
    }

private:
    T val_;
    bool has_value_{false};
    bool has_receiver_{false};
    bool stoped_{false};
    std::mutex mutex_;
    std::condition_variable send_;
    std::condition_variable recv_;
};
