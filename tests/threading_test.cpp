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

class BorrowTarget;

class BorrowLocator : public servicelocator::ServiceLocator<BorrowLocator, 8> {
public:
    SL_FAIL_ON_LOCATOR_ERROR

    ~BorrowLocator() { TearDown(); }

    BorrowTarget* helper_target = nullptr;
};

class BorrowTarget : public servicelocator::Service {
public:
    explicit BorrowTarget(BorrowLocator&) {}
};

class BorrowWaiter : public servicelocator::Service {
public:
    explicit BorrowWaiter(BorrowLocator& locator) : locator_(locator) {}
    bool ShouldRegister() override {
        const std::thread::id lender = std::this_thread::get_id();
        std::thread helper([this, lender] {
            BorrowLocator::ResolverBorrow borrow(locator_, lender);
            locator_.helper_target = locator_.TryGet<BorrowTarget>();
        });
        helper.join();
        return true;
    }

private:
    BorrowLocator& locator_;
};

}

SERVICELOCATOR_REGISTER(ThreadLocator, Spawner);
SERVICELOCATOR_REGISTER(ThreadLocator, Slow);
SERVICELOCATOR_REGISTER(BorrowLocator, BorrowWaiter);
SERVICELOCATOR_REGISTER(BorrowLocator, BorrowTarget);

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

static void WaitedJobResolvesWithBorrowedRole() {
    BorrowLocator locator;
    locator.Bootstrap();
    SL_CHECK(locator.helper_target != nullptr);
    SL_CHECK(locator.helper_target == &locator.Get<BorrowTarget>());
}

int main() {
    ConcurrentFirstGetRunsOnReadyOnce();
    GetAfterBootstrapIsSharedAcrossThreads();
    ResolveOnOneThreadReadyOnAnother();
    WaitedJobResolvesWithBorrowedRole();
    std::printf("threading_test passed\n");
    return 0;
}
