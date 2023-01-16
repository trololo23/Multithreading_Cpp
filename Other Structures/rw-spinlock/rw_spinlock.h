#pragma once

#include <atomic>
#include <iostream>

struct RWSpinLock {
    void LockRead() {
        int snapshot;
        do {
            snapshot = state_.load();
        } while (snapshot < 0 || !state_.compare_exchange_weak(snapshot, snapshot + 1));
    }

    void UnlockRead() {
        state_.fetch_sub(1);
    }

    void LockWrite() {
        int snapshot = 0;
        while (!state_.compare_exchange_weak(snapshot, -1)) {
            snapshot = 0;
        }
    }

    void UnlockWrite() {
        state_.fetch_add(1);
    }

private:
    std::atomic<int> state_ = 0;
};
