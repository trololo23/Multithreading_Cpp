#pragma once

#include <queue>
#include <atomic>
#include <iostream>

template <class T>
class MPMCBoundedQueue {
    struct Cell {
        T value;
        std::atomic<size_t> generation;
    };

public:
    explicit MPMCBoundedQueue(size_t size) : size_(size), mask_(size_ - 1) {
        arr_ = new Cell[size];
        for (size_t i = 0; i < size_; ++i) {
            arr_[i].generation.store(i);
        }
    }

    bool Enqueue(const T& value) {
        size_t pos = end_.load();
        while (true) {
            int64_t diff = (arr_[pos & mask_].generation).load() - pos;
            if (diff < 0) {
            }
            if (!diff) {
                if (end_.compare_exchange_strong(pos, pos + 1)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // заполнили стек
            } else {
                pos = end_.load();
            }
        }
        arr_[pos & mask_].value = value;
        (arr_[pos & mask_].generation).store(pos + 1);
        return true;
    }

    bool Dequeue(T& data) {
        size_t pos = begin_.load();
        while (true) {
            int64_t diff = (arr_[pos & mask_].generation).load() - (pos + 1);
            if (!diff) {
                if (begin_.compare_exchange_strong(pos, pos + 1)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // не записали ещё, то есть очередь пустая
            } else {
                pos = begin_.load();
            }
        }

        data = std::move(arr_[pos & mask_].value);
        (arr_[pos & mask_].generation).store(pos + size_);
        return true;
    }

    ~MPMCBoundedQueue() {
        delete[] arr_;
    }

private:
    std::atomic<size_t> end_ = 0;
    std::atomic<size_t> begin_ = 0;
    Cell* arr_;
    size_t size_;
    size_t mask_;
};
