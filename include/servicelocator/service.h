#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#if defined(_MSC_VER)
#define SERVICELOCATOR_NOINLINE __declspec(noinline)
#else
#define SERVICELOCATOR_NOINLINE __attribute__((noinline))
#endif

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

    enum class ReadyResult { Ready, Cycle, Failed };

    class ReadyRun {
    public:
        explicit ReadyRun(Service& service) : service_(service) {
            service_.owner_.store(std::this_thread::get_id(), std::memory_order_release);
        }
        ~ReadyRun() {
            if (!finished_) service_.failed_.store(true, std::memory_order_relaxed);
            service_.owner_.store(std::thread::id{}, std::memory_order_release);
        }
        ReadyRun(const ReadyRun&)            = delete;
        ReadyRun& operator=(const ReadyRun&) = delete;

        void Finish() {
            service_.ready_.store(true, std::memory_order_release);
            finished_ = true;
        }

    private:
        Service& service_;
        bool     finished_ = false;
    };

    ReadyResult EnsureReady() {
        if (ready_.load(std::memory_order_acquire)) return ReadyResult::Ready;
        return RunFirstReady();
    }

    SERVICELOCATOR_NOINLINE ReadyResult RunFirstReady() {
        if (owner_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
            return ReadyResult::Cycle;
        }
        std::lock_guard<std::mutex> lk(ready_mtx_);
        if (ready_.load(std::memory_order_relaxed)) return ReadyResult::Ready;
        if (failed_.load(std::memory_order_relaxed)) return ReadyResult::Failed;
        ReadyRun run(*this);
        OnReady();
        run.Finish();
        return ReadyResult::Ready;
    }

    bool BeginShutdown() {
        if (!ready_.load(std::memory_order_acquire) && !failed_.load(std::memory_order_acquire)) return false;
        bool expected = false;
        return shutdown_ran_.compare_exchange_strong(expected, true);
    }

    std::atomic<bool>            ready_{false};
    std::atomic<bool>            shutdown_ran_{false};
    std::atomic<bool>            failed_{false};
    std::mutex                   ready_mtx_;
    std::atomic<std::thread::id> owner_{};
};

}
