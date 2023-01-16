#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <list>
#include<iostream>

template <class K, class V, class Hash = std::hash<K>>
class ConcurrentHashMap {
public:
    ConcurrentHashMap(const Hash& hasher = Hash()) : ConcurrentHashMap(kUndefinedSize, hasher) {
    }

    explicit ConcurrentHashMap(int expected_size, const Hash& hasher = Hash())
        : ConcurrentHashMap(expected_size, kDefaultConcurrencyLevel, hasher) {
    }

    ConcurrentHashMap(int expected_size, int expected_threads_count, const Hash& hasher = Hash())
        : hash_(hasher), mutex_(std::vector <std::mutex> (50)){ // NOLINT
        if (expected_size != kUndefinedSize) {
            data_.resize(expected_size);
        } else {
            data_.resize(1);
        }
        bucket_size_.store(data_.size());
        size_ = 0;
    }

    void LockAll() {
        for (auto &u: mutex_) {
            u.lock();
        }
    }

    void UnLockAll() {
        for (auto &u: mutex_) {
            u.unlock();
        }
    }

    void Rehash(size_t bucket_ind) {
        if (!(data_[bucket_ind].size() > 50)) {
            return;
        }
        LockAll();
        std::vector <std::list<std::pair<K, V>>> new_data(data_.size() * 3);
        for (auto &bucket : data_) {
            for (auto &u: bucket) {
                size_t hash = hash_(u.first);
                size_t bucket_ind = hash % new_data.size();
                new_data[bucket_ind].push_back(u);
            }
        }
        data_.swap(new_data);
        UnLockAll();
    }

    bool Insert(const K& key, const V& value) {
        size_t h = hash_(key);
        size_t bucket_ind = h % bucket_size_.load();
        size_t mutex_ind = bucket_ind % mutex_.size();
        bool ans = true;
        {
            std::scoped_lock lock(mutex_[mutex_ind]);

//            size_t bucket_ind = h % data_.size();

            /// add class bucket
            for (auto &u: data_[bucket_ind]) {
                if (u.first == key) {
                    ans = false;
                }
            }

            if (ans) {
                ++size_;
                data_[bucket_ind].push_back({key, value});
            }
        }
//        lock.unlock();

//        if (data_[bucket_ind].size() > 50) {
//            Rehash(bucket_ind);
//        }

        return ans;
    }

    bool Erase(const K& key) {
        size_t h = hash_(key);
        size_t mutex_ind = h % mutex_.size();
        std::unique_lock<std::mutex> lock(mutex_[mutex_ind]);

        size_t bucket_ind = h % data_.size();

        auto it = data_[bucket_ind].begin();
        while (it != data_[bucket_ind].end() && it->first != key) {
            ++it;
        }
        if (it == data_[bucket_ind].end()) {
            return false;
        }

        data_[bucket_ind].erase(it);
        return true;
    }

    void Clear() {
        LockAll();
        data_.clear();
        data_.resize(1);
        size_ = 0;
        UnLockAll();
    }

    std::pair<bool, V> Find(const K& key) const {
        size_t h = hash_(key);
        size_t mutex_ind = h % mutex_.size();
        std::scoped_lock<std::mutex> lock(mutex_[mutex_ind]);

        size_t bucket_ind = h % data_.size();

        auto it = data_[bucket_ind].begin();
        while (it != data_[bucket_ind].end() && it->first != key) {
            ++it;
        }
        if (it == data_[bucket_ind].end()) {
            return {false, V()};
        }
        return {true, it->second};
    }

    const V At(const K& key) const {
        size_t h = hash_(key);
        size_t mutex_ind = h % mutex_.size();
        std::unique_lock<std::mutex> lock(mutex_[mutex_ind]);

        size_t bucket_ind = h % data_.size();

        auto it = data_[bucket_ind].begin();
        while (it != data_[bucket_ind].end() && it->first != key) {
            ++it;
        }
        if (it == data_[bucket_ind].end()) {
            throw std::out_of_range("");
        }
        return it->second;
    }

    size_t Size() const {
        return size_;
    }

    static const int kDefaultConcurrencyLevel;
    static const int kUndefinedSize;
    static const int kMutexCount;


private:
    std::vector <std::list<std::pair<K, V>>> data_;
    mutable std::vector <std::mutex> mutex_;
    Hash hash_;
    std::atomic <size_t> size_;
    std::atomic<size_t> bucket_size_;
};

template <class K, class V, class Hash>
const int ConcurrentHashMap<K, V, Hash>::kDefaultConcurrencyLevel = 8;

template <class K, class V, class Hash>
const int ConcurrentHashMap<K, V, Hash>::kUndefinedSize = -1;

template <class K, class V, class Hash>
const int ConcurrentHashMap<K, V, Hash>::kMutexCount = -1;