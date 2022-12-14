#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <unordered_set>

std::shared_mutex lock;

thread_local std::atomic<void*> hazard_ptr{nullptr};

struct ThreadState {
    ThreadState(std::atomic<void*>* ptr) : ptr_(ptr){};

    std::atomic<void*>* ptr_;
};

std::mutex threads_lock;
std::unordered_set<ThreadState*> threads;

template <class T>
T* Acquire(std::atomic<T*>* ptr) {
    auto value = ptr->load();

    do {
        hazard_ptr.store(value);

        auto new_value = ptr->load();
        if (new_value == value) {
            return value;
        }

        value = new_value;
    } while (true);
}

inline void Release() {
    hazard_ptr.store(nullptr);
}

struct RetiredPtr {
    void* value;
    std::function<void(void*)> deleter;
    RetiredPtr* next = nullptr;
};

std::atomic<RetiredPtr*> free_list = nullptr;
std::atomic<int> approximate_free_list_size = 0;

std::mutex scan_lock;

void ScanFreeList() {
    approximate_free_list_size.store(0);

    {
        std::lock_guard guard(scan_lock);

        RetiredPtr* retired = free_list.exchange(nullptr);

        std::vector<void*> hazard;
        {
            std::unique_lock guard(threads_lock);
            for (const auto& thread : threads) {
                if (auto ptr = thread->ptr_->load(); ptr) {
                    hazard.push_back(ptr);
                }
            }
        }

        while (retired) {
            auto it = std::find(hazard.begin(), hazard.end(), retired->value);
            if (it == hazard.end()) {
                retired->deleter(retired->value);
                auto temp = retired;
                retired = retired->next;
                delete temp;
            } else {
                auto next = retired->next;  // next "head"
                ++approximate_free_list_size;
                retired->next = nullptr;
                while (!free_list.compare_exchange_weak(
                    retired->next, retired)) {  // атомарно добавляем в free-list
                }
                retired = next;
            }
        }
    }
}

template <class T, class Deleter = std::default_delete<T>>
void Retire(T* value, Deleter deleter = {}) {
    auto delete_func = std::function([deleter](void* ptr) { deleter(reinterpret_cast<T*>(ptr)); });
    RetiredPtr* head = new RetiredPtr{value, delete_func};
    while (!free_list.compare_exchange_weak(head->next, head)) {  // атомарно добавляем в free-list
    }

    ++approximate_free_list_size;

    if (approximate_free_list_size.load() > 1000) {
        ScanFreeList();
    }
}

inline void RegisterThread() {
    std::lock_guard guard(threads_lock);
    auto thread_state_ptr = new ThreadState(&hazard_ptr);
    threads.insert(thread_state_ptr);
}

inline void UnregisterThread() {
    std::lock_guard guard(threads_lock);
    auto ptr = &hazard_ptr;
    for (auto it = threads.begin(); it != threads.end(); ++it) {
        if ((*it)->ptr_ == ptr) {
            delete (*it);
            threads.erase(it);
            break;
        }
    }

    if (threads.empty()) {
        while (free_list.load()) {
            free_list.load()->deleter(
                free_list.load()->value);  // очищаем объект под польз. указателем
            auto temp = free_list.load();
            free_list = free_list.load()->next;
            delete temp;
        }
    }
}
