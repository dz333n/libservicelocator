#include "check.h"

#include <servicelocator/service_locator.h>

#include <cstring>
#include <thread>

namespace {

template <class Self, int Capacity>
class MisuseLocator : public servicelocator::ServiceLocator<Self, Capacity> {
public:
    [[noreturn]] static void OnLocatorError(const char* message) {
        std::printf("servicelocator error: %s\n", message);
        std::fflush(stdout);
        std::exit(0);
    }
};

class NoWinnerLocator : public MisuseLocator<NoWinnerLocator, 8> {};
class NoWinnerBase : public servicelocator::Service {
public:
    explicit NoWinnerBase(NoWinnerLocator&) {}
};
class NoWinnerImpl : public NoWinnerBase {
public:
    using NoWinnerBase::NoWinnerBase;
    bool ShouldRegister() override { return false; }
};

class TwoWinnersLocator : public MisuseLocator<TwoWinnersLocator, 8> {};
class TwoWinnersBase : public servicelocator::Service {
public:
    explicit TwoWinnersBase(TwoWinnersLocator&) {}
};
class TwoWinnersFirst : public TwoWinnersBase {
public:
    using TwoWinnersBase::TwoWinnersBase;
};
class TwoWinnersSecond : public TwoWinnersBase {
public:
    using TwoWinnersBase::TwoWinnersBase;
};

class TwoFallbacksLocator : public MisuseLocator<TwoFallbacksLocator, 8> {};
class TwoFallbacksBase : public servicelocator::Service {
public:
    explicit TwoFallbacksBase(TwoFallbacksLocator&) {}
};
class TwoFallbacksFirst : public TwoFallbacksBase {
public:
    using TwoFallbacksBase::TwoFallbacksBase;
};
class TwoFallbacksSecond : public TwoFallbacksBase {
public:
    using TwoFallbacksBase::TwoFallbacksBase;
};

class RegisterCycleLocator : public MisuseLocator<RegisterCycleLocator, 8> {};
class RegisterCycle : public servicelocator::Service {
public:
    explicit RegisterCycle(RegisterCycleLocator& locator) : locator_(locator) {}
    bool ShouldRegister() override { return locator_.TryGet<RegisterCycle>() == nullptr; }

private:
    RegisterCycleLocator& locator_;
};

class ReadyCycleLocator : public MisuseLocator<ReadyCycleLocator, 8> {};
class ReadyCycleB;
class ReadyCycleA : public servicelocator::Service {
public:
    explicit ReadyCycleA(ReadyCycleLocator& locator) : locator_(locator) {}
    void OnReady() override;

private:
    ReadyCycleLocator& locator_;
};
class ReadyCycleB : public servicelocator::Service {
public:
    explicit ReadyCycleB(ReadyCycleLocator& locator) : locator_(locator) {}
    void OnReady() override { locator_.Get<ReadyCycleA>(); }

private:
    ReadyCycleLocator& locator_;
};
void ReadyCycleA::OnReady() { locator_.Get<ReadyCycleB>(); }

class OverflowLocator : public MisuseLocator<OverflowLocator, 2> {};
class OverflowFirst : public servicelocator::Service {
public:
    explicit OverflowFirst(OverflowLocator&) {}
};
class OverflowSecond : public servicelocator::Service {
public:
    explicit OverflowSecond(OverflowLocator&) {}
};

class CreateTwiceLocator : public MisuseLocator<CreateTwiceLocator, 8> {};

class TwoBasesLocator : public MisuseLocator<TwoBasesLocator, 8> {};
class TwoBasesOuter : public servicelocator::Service {
public:
    explicit TwoBasesOuter(TwoBasesLocator&) {}
};
class TwoBasesInner : public TwoBasesOuter {
public:
    using TwoBasesOuter::TwoBasesOuter;
};
class TwoBasesImpl : public TwoBasesInner {
public:
    using TwoBasesInner::TwoBasesInner;
};

class InstanceHookLocator : public servicelocator::ServiceLocator<InstanceHookLocator, 8> {
public:
    [[noreturn]] void OnLocatorError(const char* message) {
        std::printf("servicelocator error: [%s] %s\n", owner_, message);
        std::fflush(stdout);
        std::exit(0);
    }

private:
    const char* owner_ = "instance hook";
};
class InstanceHookMissing : public servicelocator::Service {
public:
    explicit InstanceHookMissing(InstanceHookLocator&) {}
};

class SecondThreadLocator : public MisuseLocator<SecondThreadLocator, 8> {};
class SecondThreadTarget : public servicelocator::Service {
public:
    explicit SecondThreadTarget(SecondThreadLocator&) {}
};
class SecondThreadProbe : public servicelocator::Service {
public:
    explicit SecondThreadProbe(SecondThreadLocator& locator) : locator_(locator) {}
    bool ShouldRegister() override {
        std::thread worker([this] { locator_.TryGet<SecondThreadTarget>(); });
        worker.join();
        return true;
    }

private:
    SecondThreadLocator& locator_;
};

}

SERVICELOCATOR_REGISTER_AS(NoWinnerLocator, NoWinnerImpl, NoWinnerBase);
SERVICELOCATOR_REGISTER_AS(TwoWinnersLocator, TwoWinnersFirst, TwoWinnersBase);
SERVICELOCATOR_REGISTER_AS(TwoWinnersLocator, TwoWinnersSecond, TwoWinnersBase);
SERVICELOCATOR_REGISTER_AS_FALLBACK(TwoFallbacksLocator, TwoFallbacksFirst, TwoFallbacksBase);
SERVICELOCATOR_REGISTER_AS_FALLBACK(TwoFallbacksLocator, TwoFallbacksSecond, TwoFallbacksBase);
SERVICELOCATOR_REGISTER(RegisterCycleLocator, RegisterCycle);
SERVICELOCATOR_REGISTER(ReadyCycleLocator, ReadyCycleA);
SERVICELOCATOR_REGISTER(ReadyCycleLocator, ReadyCycleB);
SERVICELOCATOR_REGISTER(OverflowLocator, OverflowFirst);
SERVICELOCATOR_REGISTER(OverflowLocator, OverflowSecond);
SERVICELOCATOR_REGISTER_AS(TwoBasesLocator, TwoBasesImpl, TwoBasesOuter);
[[maybe_unused]] static const bool two_bases_second_registration =
    (TwoBasesLocator::AddServiceFactory([](TwoBasesLocator& locator) {
         locator.AddCandidateAlsoSelf<TwoBasesImpl, TwoBasesInner>(
             std::make_unique<TwoBasesImpl>(locator), false);
     }), true);
SERVICELOCATOR_REGISTER(SecondThreadLocator, SecondThreadProbe);
SERVICELOCATOR_REGISTER(SecondThreadLocator, SecondThreadTarget);

static void RunScenario(const char* name) {
    if (std::strcmp(name, "no_winner") == 0) {
        NoWinnerLocator locator;
        locator.Bootstrap();
        locator.Get<NoWinnerBase>();
    } else if (std::strcmp(name, "two_winners") == 0) {
        TwoWinnersLocator locator;
        locator.Bootstrap();
    } else if (std::strcmp(name, "two_fallbacks") == 0) {
        TwoFallbacksLocator locator;
        locator.Bootstrap();
    } else if (std::strcmp(name, "register_cycle") == 0) {
        RegisterCycleLocator locator;
        locator.Bootstrap();
    } else if (std::strcmp(name, "ready_cycle") == 0) {
        ReadyCycleLocator locator;
        locator.Bootstrap();
    } else if (std::strcmp(name, "overflow") == 0) {
        OverflowLocator locator;
        locator.Bootstrap();
    } else if (std::strcmp(name, "create_twice") == 0) {
        CreateTwiceLocator locator;
        locator.CreateAllServices();
        locator.CreateAllServices();
    } else if (std::strcmp(name, "two_bases") == 0) {
        TwoBasesLocator locator;
        locator.Bootstrap();
    } else if (std::strcmp(name, "instance_hook") == 0) {
        InstanceHookLocator locator;
        locator.Bootstrap();
        locator.Get<InstanceHookMissing>();
    } else if (std::strcmp(name, "second_thread") == 0) {
        SecondThreadLocator locator;
        locator.Bootstrap();
    } else {
        std::printf("unknown scenario %s\n", name);
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: misuse_test <scenario>\n");
        return 1;
    }
    RunScenario(argv[1]);
    std::printf("NO LOCATOR ERROR\n");
    return 1;
}
