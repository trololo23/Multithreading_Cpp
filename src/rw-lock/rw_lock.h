#pragma once
#include <mutex>
#include <iostream>
#include <condition_variable>

class RWLock {
public:
    template <class Func>
    void Read(Func func) {
        {
            std::unique_lock lock(global_);
            ++blocked_readers_;
        }

        try {
            func();
        } catch (...) {
            EndRead();
            throw;
        }
        EndRead();
    }

    template <class Func>
    void Write(Func func) {
        std::unique_lock<std::mutex> lock(global_);
        while (blocked_readers_ > 0) {
            dont_read_.wait(lock);
        }
        func();
        dont_read_.notify_one();
    }

private:
    std::mutex global_;
    std::condition_variable dont_read_;
    int blocked_readers_ = 0;

    void EndRead() {
        std::unique_lock lock(global_);
        --blocked_readers_;
        if (!blocked_readers_) {
            dont_read_.notify_one();
        }
    }
};
