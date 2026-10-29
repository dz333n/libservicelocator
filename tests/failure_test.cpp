#include "check.h"

#include <servicelocator/service_locator.h>

#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace {

using EventLog = std::vector<std::string>;

struct StartFailure {
    std::string reason;
};

class FailureLocator : public servicelocator::ServiceLocator<FailureLocator, 64> {
public:
    explicit FailureLocator(EventLog& event_log) : log(event_log) {}
    ~FailureLocator() { TearDown(); }

    void ShutDownService(servicelocator::Service& service) {
        try {
            service.OnShutdown();
        } catch (const StartFailure& failure) {
            log.push_back("shutdown failed: " + failure.reason);
        }
    }

    EventLog& log;
    bool      refuse_selection = false;
};

class Healthy : public servicelocator::Service {
public:
    explicit Healthy(FailureLocator& locator) : locator_(locator) {}
    void OnReady() override { locator_.log.push_back("Healthy.ready"); }
    void OnShutdown() override { locator_.log.push_back("Healthy.shutdown"); }

private:
    FailureLocator& locator_;
};

class Broken : public servicelocator::Service {
public:
    explicit Broken(FailureLocator& locator) : locator_(locator) {}
    void OnReady() override {
        locator_.log.push_back("Broken.ready");
        throw StartFailure{"broken"};
    }
    void OnShutdown() override { locator_.log.push_back("Broken.shutdown"); }

private:
    FailureLocator& locator_;
};

class NeedsBroken : public servicelocator::Service {
public:
    explicit NeedsBroken(FailureLocator& locator) : locator_(locator) {}
    void OnReady() override { locator_.Get<Broken>(); }

private:
    FailureLocator& locator_;
};

class BrokenShutdown : public servicelocator::Service {
public:
    explicit BrokenShutdown(FailureLocator& locator) : locator_(locator) {}
    void OnShutdown() override {
        locator_.log.push_back("BrokenShutdown.shutdown");
        throw StartFailure{"stop"};
    }

private:
    FailureLocator& locator_;
};

class Selection : public servicelocator::Service {
public:
    using Service::Service;
};

class RoutingLocator;

struct RoutedError {
    std::string           what;
    const RoutingLocator* locator;
};

class RoutingLocator : public servicelocator::ServiceLocator<RoutingLocator, 8> {
public:
    ~RoutingLocator() { TearDown(); }

    [[noreturn]] void RaiseLocatorError(const std::string& message) { throw RoutedError{message, this}; }
};

class Unregistered : public servicelocator::Service {};

class PlainLocator : public servicelocator::ServiceLocator<PlainLocator, 8> {
public:
    explicit PlainLocator(EventLog& event_log) : log(&event_log) {}

    EventLog* log;
};

class PlainShutdown : public servicelocator::Service {
public:
    explicit PlainShutdown(PlainLocator& locator) : log_(*locator.log) {}
    void OnShutdown() override { log_.push_back("PlainShutdown.shutdown"); }

private:
    EventLog& log_;
};

class RefusingSelection : public Selection {
public:
    explicit RefusingSelection(FailureLocator& locator) : locator_(locator) {}
    bool ShouldRegister() override {
        if (locator_.refuse_selection) throw StartFailure{"selection"};
        return true;
    }

private:
    FailureLocator& locator_;
};

}

SERVICELOCATOR_REGISTER(FailureLocator, Healthy);
SERVICELOCATOR_REGISTER(FailureLocator, Broken);
SERVICELOCATOR_REGISTER(FailureLocator, NeedsBroken);
SERVICELOCATOR_REGISTER(FailureLocator, BrokenShutdown);
SERVICELOCATOR_REGISTER_AS(FailureLocator, RefusingSelection, Selection);
SERVICELOCATOR_REGISTER(PlainLocator, PlainShutdown);

template <class Fn>
static std::string FailureOf(Fn&& fn) {
    try {
        fn();
    } catch (const StartFailure& failure) {
        return failure.reason;
    } catch (const servicelocator::LocatorError&) {
        return "locator";
    }
    return "";
}

static long Count(const EventLog& log, const std::string& event) {
    long count = 0;
    for (const std::string& entry : log) count += entry == event ? 1 : 0;
    return count;
}

static void FailedReadyFailsLaterGetsWithoutRunningAgain() {
    EventLog       log;
    FailureLocator locator(log);
    locator.CreateAllServices();
    SL_CHECK(FailureOf([&] { locator.Get<Broken>(); }) == "broken");
    SL_CHECK(FailureOf([&] { locator.Get<Broken>(); }) == "locator");
    SL_CHECK(FailureOf([&] { locator.Get<NeedsBroken>(); }) == "locator");
    std::string other_thread;
    std::thread([&] { other_thread = FailureOf([&] { locator.Get<Broken>(); }); }).join();
    SL_CHECK(other_thread == "locator");
    SL_CHECK(Count(log, "Broken.ready") == 1);
}

static void FailedServiceGetsShutdown() {
    EventLog log;
    {
        FailureLocator locator(log);
        locator.CreateAllServices();
        locator.Get<Healthy>();
        SL_CHECK(FailureOf([&] { locator.Get<Broken>(); }) == "broken");
        SL_CHECK(FailureOf([&] { locator.Get<NeedsBroken>(); }) == "locator");
    }
    SL_CHECK(Count(log, "Healthy.shutdown") == 1);
    SL_CHECK(Count(log, "Broken.shutdown") == 1);
}

static void UnreadyServiceGetsNoShutdown() {
    EventLog log;
    {
        FailureLocator locator(log);
        locator.CreateAllServices();
        locator.Get<Healthy>();
    }
    SL_CHECK(Count(log, "Healthy.shutdown") == 1);
    SL_CHECK(Count(log, "Broken.shutdown") == 0);
    SL_CHECK(Count(log, "BrokenShutdown.shutdown") == 0);
}

static void ShutdownGoesThroughTheDerivedLocator() {
    EventLog log;
    {
        FailureLocator locator(log);
        locator.CreateAllServices();
        locator.Get<Healthy>();
        locator.Get<BrokenShutdown>();
    }
    SL_CHECK(Count(log, "BrokenShutdown.shutdown") == 1);
    SL_CHECK(Count(log, "shutdown failed: stop") == 1);
    SL_CHECK(Count(log, "Healthy.shutdown") == 1);
}

static void LocatorWithoutTearDownShutsDownInItsBase() {
    EventLog log;
    {
        PlainLocator locator(log);
        locator.CreateAllServices();
        locator.Get<PlainShutdown>();
    }
    SL_CHECK(Count(log, "PlainShutdown.shutdown") == 1);
}

static void FailedSelectionRepeatsItsOwnFailure() {
    EventLog       log;
    FailureLocator locator(log);
    locator.CreateAllServices();
    locator.refuse_selection = true;
    SL_CHECK(FailureOf([&] { locator.Get<Selection>(); }) == "selection");
    SL_CHECK(FailureOf([&] { locator.Get<Selection>(); }) == "selection");
    locator.refuse_selection = false;
    Selection& selected = locator.Get<Selection>();
    SL_CHECK(&selected == &locator.Get<RefusingSelection>());
}

static void LocatorErrorGoesThroughTheDerivedRaise() {
    RoutingLocator locator;
    locator.CreateAllServices();
    std::string           routed;
    const RoutingLocator* raised_by = nullptr;
    try {
        locator.Get<Unregistered>();
    } catch (const RoutedError& error) {
        routed    = error.what;
        raised_by = error.locator;
    }
    SL_CHECK(routed.find("Get<") == 0);
    SL_CHECK(raised_by == &locator);
}

int main() {
    FailedReadyFailsLaterGetsWithoutRunningAgain();
    FailedServiceGetsShutdown();
    UnreadyServiceGetsNoShutdown();
    ShutdownGoesThroughTheDerivedLocator();
    LocatorWithoutTearDownShutsDownInItsBase();
    FailedSelectionRepeatsItsOwnFailure();
    LocatorErrorGoesThroughTheDerivedRaise();
    std::printf("failure_test passed\n");
    return 0;
}
