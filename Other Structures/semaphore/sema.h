#pragma once

#include <mutex>
#include <condition_variable>
#include <deque>
#include <iostream>

class DefaultCallback {
public:
    void operator()(int& value) {
        --value;
    }
};

class Semaphore {
public:
    Semaphore(int count) : count_(count) {
    }

    void Leave() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ++count_;
        }
        cv_.notify_all();
    }

    template <class Func>
    void Enter(Func callback) {
        std::unique_lock<std::mutex> lock(mutex_);

        bool need_to_inc = false;
        int turn;

        if (!count_) {
            turn = order_++;
            need_to_inc = true;
        }

        while (count_ == 0 && turn != turn_) {
            cv_.wait(lock);
        }

        turn_ += need_to_inc;

        callback(count_);
    }

    void Enter() {
        DefaultCallback callback;
        Enter(callback);
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_ = 0;

    int order_ = 0;
    int turn_ = 0;
};
