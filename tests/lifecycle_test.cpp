#include "check.h"

#include <servicelocator/deferred_service.h>
#include <servicelocator/service_locator.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using EventLog = std::vector<std::string>;

class LoggedService;

class LifecycleLocator : public servicelocator::ServiceLocator<LifecycleLocator, 64> {
public:
    SL_FAIL_ON_LOCATOR_ERROR

    explicit LifecycleLocator(EventLog& event_log) : log(event_log) {}
    ~LifecycleLocator() { TearDown(); }

    static void OnServiceDestroy(const servicelocator::Service& service);

    EventLog& log;
};

class LoggedService : public servicelocator::Service {
public:
    LoggedService(LifecycleLocator& locator, const char* name) : locator_(locator), name_(name) {}
    ~LoggedService() override { Record("destroy"); }

    void OnReady() override { Record("ready"); }
    void OnShutdown() override { Record("shutdown"); }

    void Record(const char* event) const { locator_.log.push_back(name_ + "." + event); }

protected:
    LifecycleLocator& locator_;
    std::string       name_;
};

void LifecycleLocator::OnServiceDestroy(const servicelocator::Service& service) {
    dynamic_cast<const LoggedService&>(service).Record("hook");
}

class Memory : public LoggedService {
public:
    explicit Memory(LifecycleLocator& locator) : LoggedService(locator, "Memory") {}
};

class Engine : public LoggedService {
public:
    explicit Engine(LifecycleLocator& locator) : LoggedService(locator, "Engine") {}
    void OnReady() override {
        locator_.Get<Memory>();
        LoggedService::OnReady();
    }
};

class Idle : public LoggedService {
public:
    explicit Idle(LifecycleLocator& locator) : LoggedService(locator, "Idle") {}
};

}

SERVICELOCATOR_REGISTER(LifecycleLocator, Memory);
SERVICELOCATOR_REGISTER(LifecycleLocator, Engine);
SERVICELOCATOR_REGISTER(LifecycleLocator, Idle);

static long Count(const EventLog& log, const std::string& event) {
    return static_cast<long>(std::count(log.begin(), log.end(), event));
}

static std::size_t IndexOf(const EventLog& log, const std::string& event) {
    return static_cast<std::size_t>(std::find(log.begin(), log.end(), event) - log.begin());
}

static void GetReadiesDependenciesFirst() {
    EventLog log;
    {
        LifecycleLocator locator(log);
        locator.CreateAllServices();
        locator.Get<Engine>();
        locator.Get<Engine>();
        SL_CHECK((log == EventLog{"Memory.ready", "Engine.ready"}));
    }
    SL_CHECK(Count(log, "Idle.destroy") == 1);
    SL_CHECK(Count(log, "Idle.shutdown") == 0);
    SL_CHECK(Count(log, "Idle.hook") == 0);
}

static void BootstrapReadiesEveryWinnerOnce() {
    EventLog log;
    LifecycleLocator locator(log);
    locator.Bootstrap();
    locator.Get<Memory>();
    SL_CHECK(Count(log, "Memory.ready") == 1);
    SL_CHECK(Count(log, "Engine.ready") == 1);
    SL_CHECK(Count(log, "Idle.ready") == 1);
    SL_CHECK(IndexOf(log, "Memory.ready") < IndexOf(log, "Engine.ready"));
}

static void ShutdownRunsBeforeAnyDestroy() {
    EventLog log;
    {
        LifecycleLocator locator(log);
        locator.Bootstrap();
        log.clear();
    }
    EventLog shutdowns;
    EventLog destroys;
    for (const std::string& event : log) {
        const std::string name = event.substr(0, event.find('.'));
        if (event.find(".shutdown") != std::string::npos) shutdowns.push_back(name);
        if (event.find(".destroy") != std::string::npos) destroys.push_back(name);
    }
    SL_CHECK(shutdowns.size() == 3);
    SL_CHECK(shutdowns == destroys);
    SL_CHECK(IndexOf(log, shutdowns.back() + ".shutdown") < IndexOf(log, destroys.front() + ".destroy"));
    for (const std::string& name : destroys) {
        SL_CHECK(IndexOf(log, name + ".hook") + 1 == IndexOf(log, name + ".destroy"));
    }
}

static void DeferredServiceResolvesOnFirstUse() {
    EventLog log;
    LifecycleLocator locator(log);
    locator.CreateAllServices();
    const servicelocator::DeferredService<LifecycleLocator, Memory> deferred(locator);
    SL_CHECK(log.empty());
    Memory& memory = deferred.Get();
    SL_CHECK(&memory == &locator.Get<Memory>());
    SL_CHECK(&memory == &deferred.Get());
    SL_CHECK(Count(log, "Memory.ready") == 1);
}

int main() {
    GetReadiesDependenciesFirst();
    BootstrapReadiesEveryWinnerOnce();
    ShutdownRunsBeforeAnyDestroy();
    DeferredServiceResolvesOnFirstUse();
    std::printf("lifecycle_test passed\n");
    return 0;
}
