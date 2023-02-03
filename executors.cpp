#include "executors.h"

void Task::Invoke() {
    for (const auto& dep : dependences_) {
        if (!dep->IsFinished()) {
            return;
        }
    }

    if (!triggers_.empty()) {
        bool any_triggered{false};
        for (const auto& trig : triggers_) {
            if (trig->IsFinished()) {
                any_triggered = true;
                break;
            }
        }
        if (!any_triggered) {
            return;
        }
    }

    if (std::chrono::system_clock::now() < ded_) {
        return;
    }

    auto guard = std::lock_guard(doing_work_);
    if (is_canceled_.load()) {
        return;
    }
    try {
        Run();
    } catch (...) {
        is_failed_.store(true);
        is_finished_.store(true);
        exc_ptr_ = std::current_exception();
        task_done_.notify_all();
        return;
    }
    is_completed_ = true;
    is_finished_.store(true);
    task_done_.notify_all();
}

void Task::AddDependency(std::shared_ptr <Task> dep) {
    dependences_.push_back(dep);
}

void Task::AddTrigger(std::shared_ptr <Task> dep) {
    triggers_.push_back(dep);
}

void Task::SetTimeTrigger(std::chrono::system_clock::time_point at) {
    ded_ = at;
}

bool Task::IsCompleted() {
    return is_completed_;
}

bool Task::IsFailed() {
    return is_failed_.load();
}

bool Task::IsCanceled() {
    return is_canceled_.load();
}

bool Task::IsFinished() {
    return is_finished_.load();
}

std::exception_ptr Task::GetError() {
    return exc_ptr_;
}

void Task::Cancel() {
    is_canceled_.store(true);
    is_finished_.store(true);
}

void Task::Wait() {
    auto guard = std::unique_lock(doing_work_);
    while (!is_finished_.load()) {
        task_done_.wait(guard);
    }
}