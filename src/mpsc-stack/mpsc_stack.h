#pragma once

#include <atomic>
#include <optional>
#include <stdexcept>
#include <utility>

template <class T>
class MPSCStack {
    struct Node {
        Node* next = nullptr;
        T value;
    };

public:
    void Push(const T& value) {
        Node* node = new Node{.next = nullptr, .value = value};

        while (!head_.compare_exchange_weak(node->next, node)) {
            // burn cpu
        }
    }

    std::optional<T> Pop() {
        Node* res = head_.load();
        do {
            if (!res) {
                return std::nullopt;
            }
        } while (!head_.compare_exchange_weak(res, res->next));

        T value = std::move(res->value);
        delete res;
        return value;
    }

    template <class TFn>
    void DequeueAll(const TFn& cb) {
        while (head_.load()) {
            auto value = Pop();
            cb(value.value());
        }
    }

    ~MPSCStack() {
        while (head_.load()) {
            Pop();
        }
    }

private:
    std::atomic<Node*> head_ = nullptr;
};
