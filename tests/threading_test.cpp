#include "check.h"

#include <servicelocator/service_locator.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

constexpr int kThreads = 8;

class ThreadLocator : public servicelocator::ServiceLocator<ThreadLocator, 16> {
public:
    SL_FAIL_ON_LOCATOR_ERROR

    ~ThreadLocator() { TearDown(); }

    std::atomic<int> slow_ready_calls{0};
};

class Slow : public servicelocator::Service {
public:
    explicit Slow(ThreadLocator& locator) : locator_(locator) {}
    void OnReady() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        locator_.slow_ready_calls.fetch_add(1);
    }

private:
    ThreadLocator& locator_;
};

class Spawner : public servicelocator::Service {
public:
    explicit Spawner(ThreadLocator& locator) : locator_(locator) {}
    void OnReady() override {
        std::vector<Slow*> seen(kThreads, nullptr);
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([this, &seen, i] { seen[i] = &locator_.Get<Slow>(); });
        }
        for (std::thread& t : threads) t.join();
        for (Slow* slow : seen) SL_CHECK(slow == seen.front() && slow != nullptr);
    }

private:
    ThreadLocator& locator_;
};

}

SERVICELOCATOR_REGISTER(ThreadLocator, Spawner);
SERVICELOCATOR_REGISTER(ThreadLocator, Slow);

static void ConcurrentFirstGetRunsOnReadyOnce() {
    ThreadLocator locator;
    locator.Bootstrap();
    SL_CHECK(locator.slow_ready_calls.load() == 1);
}

static void GetAfterBootstrapIsSharedAcrossThreads() {
    ThreadLocator locator;
    locator.Bootstrap();
    Slow* expected = &locator.Get<Slow>();
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&locator, &mismatches, expected] {
            for (int n = 0; n < 10000; ++n) {
                if (&locator.Get<Slow>() != expected) mismatches.fetch_add(1);
            }
        });
    }
    for (std::thread& t : threads) t.join();
    SL_CHECK(mismatches.load() == 0);
}

int main() {
    ConcurrentFirstGetRunsOnReadyOnce();
    GetAfterBootstrapIsSharedAcrossThreads();
    std::printf("threading_test passed\n");
    return 0;
}
