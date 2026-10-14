#include "check.h"

#include <servicelocator/service_locator.h>

namespace {

class ResolutionLocator : public servicelocator::ServiceLocator<ResolutionLocator, 64> {
public:
    SL_FAIL_ON_LOCATOR_ERROR

    ~ResolutionLocator() { TearDown(); }

    bool flash_matches = true;
    int  lcd_destroyed = 0;
};

class Clock : public servicelocator::Service {
public:
    explicit Clock(ResolutionLocator&) {}
};

class Display : public servicelocator::Service {
public:
    explicit Display(ResolutionLocator& locator) : locator_(locator) {}

protected:
    ResolutionLocator& locator_;
};

class LcdDisplay : public Display {
public:
    using Display::Display;
    ~LcdDisplay() override { ++locator_.lcd_destroyed; }
    bool ShouldRegister() override { return false; }
};

class VgaDisplay : public Display {
public:
    using Display::Display;
};

class Storage : public servicelocator::Service {
public:
    explicit Storage(ResolutionLocator& locator) : locator_(locator) {}

protected:
    ResolutionLocator& locator_;
};

class FlashStorage : public Storage {
public:
    using Storage::Storage;
    bool ShouldRegister() override { return locator_.flash_matches; }
};

class RamDiskStorage : public Storage {
public:
    using Storage::Storage;
};

class Audio : public servicelocator::Service {
public:
    explicit Audio(ResolutionLocator&) {}
};

class AbsentAudio : public Audio {
public:
    using Audio::Audio;
    bool ShouldRegister() override { return false; }
};

class Timer : public servicelocator::Service {
public:
    explicit Timer(ResolutionLocator&) {}
};

class OsTimer : public Timer {
public:
    using Timer::Timer;
};

}

SERVICELOCATOR_REGISTER(ResolutionLocator, Clock);
SERVICELOCATOR_REGISTER_AS(ResolutionLocator, LcdDisplay, Display);
SERVICELOCATOR_REGISTER_AS(ResolutionLocator, VgaDisplay, Display);
SERVICELOCATOR_REGISTER_AS(ResolutionLocator, FlashStorage, Storage);
SERVICELOCATOR_REGISTER_AS_FALLBACK(ResolutionLocator, RamDiskStorage, Storage);
SERVICELOCATOR_REGISTER_AS(ResolutionLocator, AbsentAudio, Audio);
SERVICELOCATOR_REGISTER_AS(ResolutionLocator, OsTimer, Timer);

static void PlainServiceResolvesToOneInstance() {
    ResolutionLocator locator;
    locator.CreateAllServices();
    Clock& first = locator.Get<Clock>();
    SL_CHECK(&first == &locator.Get<Clock>());
    SL_CHECK(&first == locator.TryGet<Clock>());
}

static void BaseAndConcreteReachTheWinner() {
    ResolutionLocator locator;
    locator.CreateAllServices();
    Display& display = locator.Get<Display>();
    SL_CHECK(dynamic_cast<VgaDisplay*>(&display) != nullptr);
    SL_CHECK(static_cast<Display*>(&locator.Get<VgaDisplay>()) == &display);
    SL_CHECK(locator.TryGet<LcdDisplay>() == nullptr);
    SL_CHECK(locator.lcd_destroyed == 1);
}

static void FallbackWinsOnlyWithoutPrimary() {
    ResolutionLocator with_flash;
    with_flash.CreateAllServices();
    SL_CHECK(dynamic_cast<FlashStorage*>(&with_flash.Get<Storage>()) != nullptr);
    SL_CHECK(with_flash.TryGet<RamDiskStorage>() == nullptr);

    ResolutionLocator without_flash;
    without_flash.flash_matches = false;
    without_flash.CreateAllServices();
    SL_CHECK(dynamic_cast<RamDiskStorage*>(&without_flash.Get<Storage>()) != nullptr);
    SL_CHECK(without_flash.TryGet<FlashStorage>() == nullptr);
}

static void TryGetReturnsNullWithoutWinner() {
    ResolutionLocator locator;
    locator.Bootstrap();
    SL_CHECK(locator.TryGet<Audio>() == nullptr);
    SL_CHECK(locator.TryGet<AbsentAudio>() == nullptr);
}

static void ConcreteResolvesItsBase() {
    ResolutionLocator locator;
    locator.CreateAllServices();
    OsTimer& timer = locator.Get<OsTimer>();
    SL_CHECK(static_cast<Timer*>(&timer) == &locator.Get<Timer>());
}

static void LocatorResolvesItself() {
    ResolutionLocator locator;
    locator.CreateAllServices();
    SL_CHECK(&locator.Get<ResolutionLocator>() == &locator);
}

int main() {
    PlainServiceResolvesToOneInstance();
    BaseAndConcreteReachTheWinner();
    FallbackWinsOnlyWithoutPrimary();
    TryGetReturnsNullWithoutWinner();
    ConcreteResolvesItsBase();
    LocatorResolvesItself();
    std::printf("resolution_test passed\n");
    return 0;
}
