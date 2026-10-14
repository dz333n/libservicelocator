#pragma once

#include <atomic>

namespace servicelocator {

template <class Locator, class T>
class DeferredService {
public:
    explicit DeferredService(Locator& locator) : locator_(locator) {}

    T& Get() const {
        T* service = service_.load(std::memory_order_acquire);
        if (service != nullptr) return *service;
        service = &locator_.template Get<T>();
        service_.store(service, std::memory_order_release);
        return *service;
    }

private:
    Locator&                locator_;
    mutable std::atomic<T*> service_{nullptr};
};

}
