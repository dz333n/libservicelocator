#pragma once

#include <atomic>
#include <mutex>
#include <thread>

namespace servicelocator {

template <class Derived, int Capacity>
class ServiceLocator;

class Service {
public:
    virtual ~Service() = default;

    virtual void OnReady() {}
    virtual bool ShouldRegister() { return true; }
    virtual void OnShutdown() {}

protected:
    Service() = default;

private:
    template <class Derived, int Capacity>
    friend class ServiceLocator;

    enum class ReadyResult { Ready, Cycle };

    ReadyResult EnsureReady() {
        if (ready_.load(std::memory_order_acquire)) return ReadyResult::Ready;
        if (owner_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
            return ReadyResult::Cycle;
        }
        std::lock_guard<std::mutex> lk(ready_mtx_);
        if (ready_.load(std::memory_order_relaxed)) return ReadyResult::Ready;
        owner_.store(std::this_thread::get_id(), std::memory_order_release);
        OnReady();
        ready_.store(true, std::memory_order_release);
        owner_.store(std::thread::id{}, std::memory_order_release);
        return ReadyResult::Ready;
    }

    void RunShutdown() {
        if (!ready_.load(std::memory_order_acquire)) return;
        bool expected = false;
        if (!shutdown_ran_.compare_exchange_strong(expected, true)) return;
        OnShutdown();
    }

    std::atomic<bool>            ready_{false};
    std::atomic<bool>            shutdown_ran_{false};
    std::mutex                   ready_mtx_;
    std::atomic<std::thread::id> owner_{};
};

}
