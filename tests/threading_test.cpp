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

class HelperTarget;

class HelperLocator : public servicelocator::ServiceLocator<HelperLocator, 8> {
public:
    ~HelperLocator() { TearDown(); }

    HelperTarget* helper_target = nullptr;
};

class HelperTarget : public servicelocator::Service {
public:
    explicit HelperTarget(HelperLocator&) {}
};

class HelperWaiter : public servicelocator::Service {
public:
    explicit HelperWaiter(HelperLocator& locator) : locator_(locator) {}
    bool ShouldRegister() override {
        std::thread helper([this] { locator_.helper_target = locator_.TryGet<HelperTarget>(); });
        helper.join();
        return true;
    }

private:
    HelperLocator& locator_;
};

class RaceLocator : public servicelocator::ServiceLocator<RaceLocator, 8> {
public:
    ~RaceLocator() { TearDown(); }

    std::atomic<int> picks{0};
};

class RacePick : public servicelocator::Service {
public:
    explicit RacePick(RaceLocator& locator) : locator_(locator) {}
    bool ShouldRegister() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        locator_.picks.fetch_add(1);
        return true;
    }

private:
    RaceLocator& locator_;
};

}

SERVICELOCATOR_REGISTER(ThreadLocator, Spawner);
SERVICELOCATOR_REGISTER(ThreadLocator, Slow);
SERVICELOCATOR_REGISTER(HelperLocator, HelperWaiter);
SERVICELOCATOR_REGISTER(HelperLocator, HelperTarget);
SERVICELOCATOR_REGISTER(RaceLocator, RacePick);

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

static void ResolveOnOneThreadReadyOnAnother() {
    ThreadLocator locator;
    locator.CreateAllServices();
    locator.ResolveAllSlots();
    Slow*       readied = nullptr;
    std::thread boot([&locator, &readied] {
        locator.ReadyAllServices();
        readied = &locator.Get<Slow>();
    });
    boot.join();
    SL_CHECK(locator.slow_ready_calls.load() == 1);
    SL_CHECK(&locator.Get<Slow>() == readied);
}

static void HelperThreadResolvesWhileResolverWaits() {
    HelperLocator locator;
    locator.Bootstrap();
    SL_CHECK(locator.helper_target != nullptr);
    SL_CHECK(locator.helper_target == &locator.Get<HelperTarget>());
}

static void ConcurrentFirstGetResolvesSlotOnce() {
    RaceLocator locator;
    locator.CreateAllServices();
    std::vector<RacePick*>   seen(kThreads, nullptr);
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&locator, &seen, i] { seen[i] = &locator.Get<RacePick>(); });
    }
    for (std::thread& t : threads) t.join();
    for (RacePick* pick : seen) SL_CHECK(pick == seen.front() && pick != nullptr);
    SL_CHECK(locator.picks.load() == 1);
}

int main() {
    ConcurrentFirstGetRunsOnReadyOnce();
    GetAfterBootstrapIsSharedAcrossThreads();
    ResolveOnOneThreadReadyOnAnother();
    HelperThreadResolvesWhileResolverWaits();
    ConcurrentFirstGetResolvesSlotOnce();
    std::printf("threading_test passed\n");
    return 0;
}
