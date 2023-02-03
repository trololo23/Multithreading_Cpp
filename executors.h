#include <memory>
#include <chrono>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <optional>
#include <atomic>

//////////////////////////////////////////////////////

using TimePoint = std::chrono::system_clock::time_point;

// Template UnboundedBlockingQueue
template <typename T>
class UnboundedBlockingQueue {
public:
    bool Put(T value) {
        auto guard = std::lock_guard{mutex_};
        if (stopped_) {
            return false;
        }
        buffer_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> Take() {
        auto guard = std::unique_lock{mutex_};
        not_empty_.wait(guard, [this] { return stopped_ || !buffer_.empty(); });
        if (stopped_ && buffer_.empty()) {
            return std::nullopt;
        }
        T result = std::move(buffer_.front());
        buffer_.pop_front();
        return result;
    }

    void Close() {
        CloseImpl(false);
    }

    void Cancel() {
        CloseImpl(true);
    }

private:
    void CloseImpl(bool clear) {
        auto guard = std::lock_guard{mutex_};
        stopped_ = true;
        if (clear) {
            buffer_.clear();
        }
        not_empty_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable not_empty_;

    bool stopped_{false};
    std::deque<T> buffer_;
};

class Task : public std::enable_shared_from_this<Task> {
public:
    virtual ~Task(){};

    virtual void Run() = 0;

    void Invoke();

    void AddDependency(std::shared_ptr<Task> dep);

    void AddTrigger(std::shared_ptr<Task> dep);

    void SetTimeTrigger(std::chrono::system_clock::time_point at);

    // Task::run() completed without throwing exception
    bool IsCompleted();

    // Task::run() throwed exception
    bool IsFailed();

    // Task was Canceled
    bool IsCanceled();

    // Task either completed, failed or was Canceled
    bool IsFinished();

    std::exception_ptr GetError();

    void Cancel();

    void Wait();

private:
    std::atomic<bool> is_canceled_{false};
    std::atomic<bool> is_failed_{false};
    std::atomic<bool> is_finished_{false};
    bool is_completed_{false};
    std::exception_ptr exc_ptr_;
    std::condition_variable task_done_;
    std::mutex doing_work_;
    std::vector<std::shared_ptr<Task>> dependences_;
    std::vector<std::shared_ptr<Task>> triggers_;
    TimePoint ded_{};
};

class Executor;

template <class T>
class Future : public Task {
    friend Executor;

public:
    void Run() override {
        auto guard = std::lock_guard(wait_for_value_);
        try {
            value_ = func_();
        } catch (...) {
            exc_ptr_ = std::current_exception();
        }
        got_value_.store(true);
        wait_for_value_cv_.notify_one();
    }

    T Get() {
        auto guard = std::unique_lock(wait_for_value_);
        while (!got_value_.load()) {
            wait_for_value_cv_.wait(guard);
        }
        if (exc_ptr_) {
            rethrow_exception(exc_ptr_);
        }
        return value_;
    };

private:
    void SetFunction(std::function<T()> f) {
        func_ = f;
    }

    std::function<T()> func_;
    T value_;
    std::mutex wait_for_value_;
    std::condition_variable wait_for_value_cv_;
    std::atomic<bool> got_value_{false};
    std::exception_ptr exc_ptr_;
};

template <class T>
using FuturePtr = std::shared_ptr<Future<T>>;

// Void-like type
struct Unit {};

// Template Task sheduler
class Executor {
public:
    Executor(int num_threads) {
        working_threads_ = num_threads;
        workers_.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { Run(); });
        }
    }

    void Submit(std::shared_ptr<Task> task) {
        if (is_closed_.load()) {
            task->Cancel();
            return;
        }
        queue_.Put(std::move(task));
    }

    void StartShutdown() {
        is_closed_ = true;
        queue_.Cancel();
    };

    void WaitShutdown() {
        auto guard = std::unique_lock(mutex_);
        while (working_threads_) {
            work_done_.wait(guard);
        }
    };

    template <class T>
    FuturePtr<T> Invoke(std::function<T()> fn) {
        auto future_ptr = std::make_shared<Future<T>>();
        (*future_ptr).SetFunction(fn);
        queue_.Put(future_ptr);
        return future_ptr;
    }

    template <class Y, class T>
    FuturePtr<Y> Then(FuturePtr<T> input, std::function<Y()> fn) {
        auto future_ptr = std::make_shared<Future<Y>>();
        future_ptr->AddDependency(input);
        future_ptr->SetFunction(fn);
        queue_.Put(future_ptr);
        return future_ptr;
    }

    template <class T>
    FuturePtr<std::vector<T>> WhenAll(std::vector<FuturePtr<T>> all) {
        std::function<std::vector<T>()> f = [all]() {
            std::vector<T> results(all.size());
            for (size_t i = 0; i < all.size(); ++i) {
                results[i] = all[i]->Get();
            }
            return results;
        };

        auto future_ptr = std::make_shared<Future<std::vector<T>>>();
        for (auto fut_ptr : all) {
            future_ptr->AddDependency(fut_ptr);
        }

        future_ptr->SetFunction(f);
        queue_.Put(future_ptr);
        return future_ptr;
    }

        template <class T>
        FuturePtr<T> WhenFirst(std::vector<FuturePtr<T>> all) {
            // test for cool guys
        };

    template <class T>
    FuturePtr<std::vector<T>> WhenAllBeforeDeadline(
        std::vector<FuturePtr<T>> all, std::chrono::system_clock::time_point deadline) {
        auto future_ptr = std::make_shared<Future<std::vector<T>>>();
        future_ptr->SetTimeTrigger(deadline);

        std::function<std::vector<T>()> f = [all]() {
            std::vector<T> results;
            for (auto fut_ptr : all) {
                if (fut_ptr->IsFinished()) {
                    results.push_back(fut_ptr->Get());
                }
            }
            return results;
        };

        future_ptr->SetFunction(f);
        queue_.Put(future_ptr);
        return future_ptr;
    }

    ~Executor() {
        queue_.Close();
        for (auto& t : workers_) {
            t.join();
        }
    }

private:
    void Run() {
        while (auto task = queue_.Take()) {
            task->get()->Invoke();
            if (!task->get()->IsFinished()) {
                queue_.Put(task.value());
            }
        }

        auto guard = std::lock_guard(mutex_);
        if (--working_threads_ == 0) {
            work_done_.notify_one();
        };
    }

    UnboundedBlockingQueue<std::shared_ptr<Task>> queue_;
    std::vector<std::thread> workers_;
    int working_threads_;
    std::condition_variable work_done_;
    std::mutex mutex_;
    std::atomic<bool> is_closed_{false};
};

inline std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads) {
    return std::make_shared<Executor>(num_threads);
}