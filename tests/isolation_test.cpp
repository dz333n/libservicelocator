#include "check.h"

#include <servicelocator/service_locator.h>

namespace {

class EngineLocator : public servicelocator::ServiceLocator<EngineLocator, 16> {
public:
    SL_FAIL_ON_LOCATOR_ERROR
};

class FrontendLocator : public servicelocator::ServiceLocator<FrontendLocator, 16> {
public:
    SL_FAIL_ON_LOCATOR_ERROR
};

class Translator : public servicelocator::Service {
public:
    explicit Translator(EngineLocator&) {}
};

class Window : public servicelocator::Service {
public:
    explicit Window(FrontendLocator&) {}
};

}

SERVICELOCATOR_REGISTER(EngineLocator, Translator);
SERVICELOCATOR_REGISTER(FrontendLocator, Window);

static void LocatorTypesKeepSeparateRegistries() {
    EngineLocator engine;
    FrontendLocator frontend;
    engine.Bootstrap();
    frontend.Bootstrap();
    SL_CHECK(engine.TryGet<Translator>() != nullptr);
    SL_CHECK(engine.TryGet<Window>() == nullptr);
    SL_CHECK(frontend.TryGet<Window>() != nullptr);
    SL_CHECK(frontend.TryGet<Translator>() == nullptr);
}

static void InstancesOfOneTypeShareNothing() {
    EngineLocator first;
    EngineLocator second;
    first.Bootstrap();
    second.Bootstrap();
    SL_CHECK(&first.Get<Translator>() != &second.Get<Translator>());
}

int main() {
    LocatorTypesKeepSeparateRegistries();
    InstancesOfOneTypeShareNothing();
    std::printf("isolation_test passed\n");
    return 0;
}
