#pragma once

#include "service.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace servicelocator {

template <class Derived, int Capacity>
class ServiceLocator {
    static_assert(Capacity > 0, "ServiceLocator Capacity must be positive");

public:
    using Factory = void (*)(Derived&);

    ServiceLocator(const ServiceLocator&) = delete;
    ServiceLocator& operator=(const ServiceLocator&) = delete;

    template <class T>
    T& Get() {
        T* svc = Find<T>();
        if (svc == nullptr) {
            Fail(std::string("Get<") + typeid(T).name() +
                 ">() - no candidate's ShouldRegister returned true. Either no "
                 "impl was registered for this Base, or every registered impl "
                 "decided it does not match.");
        }
        MakeReady(svc);
        return *svc;
    }

    template <class T>
    T* TryGet() {
        T* svc = Find<T>();
        if (svc != nullptr) MakeReady(svc);
        return svc;
    }

    void CreateAllServices() {
        if (services_created_) Fail("CreateAllServices() ran twice on one locator.");
        services_created_ = true;
        Derived& self = static_cast<Derived&>(*this);
        services_[SlotFor<Derived>()].store(static_cast<void*>(&self), std::memory_order_release);
        for (Factory factory : Factories()) factory(self);
    }

    void Bootstrap() {
        CreateAllServices();
        ResolveAllSlots();
        for (std::size_t i = 0; i < owned_.size(); ++i) MakeReady(owned_[i].get());
    }

    static void AddServiceFactory(Factory factory) { Factories().push_back(factory); }

    template <class Base>
    void AddCandidate(std::unique_ptr<Base> instance, bool fallback) {
        static_assert(std::is_base_of_v<Service, Base>, "AddCandidate Base must derive from Service.");
        const int slot = SlotFor<Base>();
        Base* base_ptr = instance.get();
        candidate_slots_[slot].entries.push_back(CandidateEntry{
            std::move(instance), static_cast<void*>(base_ptr), fallback, kNoSlot, nullptr});
        candidate_slots_[slot].pending.store(true, std::memory_order_release);
    }

    template <class Concrete, class Base>
    void AddCandidateAlsoSelf(std::unique_ptr<Concrete> instance, bool fallback) {
        static_assert(std::is_base_of_v<Base, Concrete>, "AddCandidateAlsoSelf Concrete must derive from Base.");
        static_assert(std::is_base_of_v<Service, Base>, "AddCandidateAlsoSelf Base must derive from Service.");
        const int base_slot     = SlotFor<Base>();
        const int concrete_slot = SlotFor<Concrete>();
        int& link = candidate_slots_[concrete_slot].base_slot;
        if (link != kNoSlot && link != base_slot) {
            Fail(std::string(typeid(Concrete).name()) +
                 " is registered as a candidate for two different Base slots.");
        }
        link = base_slot;
        Concrete* concrete_ptr = instance.get();
        Base*     base_ptr     = concrete_ptr;
        candidate_slots_[base_slot].entries.push_back(CandidateEntry{
            std::move(instance), static_cast<void*>(base_ptr), fallback,
            concrete_slot, static_cast<void*>(concrete_ptr)});
        candidate_slots_[base_slot].pending.store(true, std::memory_order_release);
    }

    static void OnServiceDestroy(const Service&) {}

protected:
    ServiceLocator() = default;
    ~ServiceLocator() { TearDown(); }

    void TearDown() {
        for (auto it = owned_.rbegin(); it != owned_.rend(); ++it) (*it)->RunShutdown();
        for (auto it = owned_.rbegin(); it != owned_.rend(); ++it) {
            Derived::OnServiceDestroy(**it);
            it->reset();
        }
        owned_.clear();
        for (CandidateSlot& cs : candidate_slots_) cs.entries.clear();
    }

private:
    static constexpr int kNoSlot = -1;

    struct CandidateEntry {
        std::unique_ptr<Service> instance;
        void*                    slot_ptr;
        bool                     is_fallback;
        int                      concrete_slot;
        void*                    concrete_ptr;
    };

    struct CandidateSlot {
        std::vector<CandidateEntry> entries;
        std::atomic<bool>           pending{false};
        bool                        resolving = false;
        int                         base_slot = kNoSlot;
    };

    [[noreturn]] void Fail(const std::string& message) {
        if constexpr (std::is_member_function_pointer_v<decltype(&Derived::OnLocatorError)>) {
            static_cast<Derived&>(*this).OnLocatorError(message.c_str());
        } else {
            Derived::OnLocatorError(message.c_str());
        }
    }

    static std::vector<Factory>& Factories() {
        static std::vector<Factory> factories;
        return factories;
    }

    int AllocateSlot(const char* type_name) {
        const int slot = next_slot_.fetch_add(1, std::memory_order_relaxed);
        if (slot >= Capacity) {
            Fail(std::string("Service slot overflow: slot ") + std::to_string(slot) +
                 " >= Capacity " + std::to_string(Capacity) + " (adding " + type_name +
                 "). Raise the locator's Capacity.");
        }
        return slot;
    }

    template <class T>
    int SlotFor() {
        static const int slot = AllocateSlot(typeid(T).name());
        return slot;
    }

    static int SlotCount() {
        return (std::min)(next_slot_.load(std::memory_order_acquire), Capacity);
    }

    template <class T>
    T* Find() {
        const int slot = SlotFor<T>();
        void* found = services_[slot].load(std::memory_order_acquire);
        if (found == nullptr) {
            ResolveSlot(slot, typeid(T).name());
            found = services_[slot].load(std::memory_order_acquire);
        }
        return static_cast<T*>(found);
    }

    template <class T>
    void MakeReady(T* svc) {
        if constexpr (std::is_base_of_v<Service, T>) {
            Service* base = svc;
            if (base->EnsureReady() == Service::ReadyResult::Cycle) {
                Fail(std::string("Cycle in the service OnReady graph at ") + typeid(*base).name() + ".");
            }
        }
    }

    void ClaimResolverThread(const char* requester) {
        const std::thread::id me = std::this_thread::get_id();
        std::thread::id expected{};
        if (resolver_thread_.compare_exchange_strong(expected, me, std::memory_order_acq_rel)) return;
        if (expected == me) return;
        Fail(std::string("Slot for ") + requester +
             " needs resolution on a second thread. Slot resolution runs on one "
             "thread; other threads may Get only after Bootstrap() returns.");
    }

    void ResolveAllSlots() {
        const int count = SlotCount();
        for (int slot = 0; slot < count; ++slot) {
            if (candidate_slots_[slot].pending.load(std::memory_order_acquire)) {
                ResolveSlot(slot, "(sweep)");
            }
        }
    }

    CandidateEntry* PickWinner(CandidateSlot& cs, bool fallback, const char* requester) {
        CandidateEntry* winner = nullptr;
        for (CandidateEntry& e : cs.entries) {
            if (e.is_fallback != fallback) continue;
            if (!e.instance->ShouldRegister()) continue;
            if (winner != nullptr) {
                Fail(std::string(fallback ? "Two fallback" : "Two") +
                     " ShouldRegister returned true for the same Base (slot resolved as " +
                     requester + "). Both '" + typeid(*winner->instance).name() + "' and '" +
                     typeid(*e.instance).name() + "' returned true.");
            }
            winner = &e;
        }
        return winner;
    }

    void ResolveSlot(int slot, const char* requester) {
        CandidateSlot& cs = candidate_slots_[slot];
        if (!cs.pending.load(std::memory_order_acquire)) {
            if (cs.base_slot != kNoSlot) ResolveSlot(cs.base_slot, requester);
            return;
        }
        ClaimResolverThread(requester);
        if (cs.resolving) {
            Fail(std::string("ShouldRegister cycle while resolving the slot for ") + requester +
                 " - a candidate's ShouldRegister called Get<>() back into a Base that "
                 "is itself being resolved.");
        }
        cs.resolving = true;

        CandidateEntry* winner = PickWinner(cs, false, requester);
        if (winner == nullptr) winner = PickWinner(cs, true, requester);

        if (winner != nullptr) {
            services_[slot].store(winner->slot_ptr, std::memory_order_release);
            if (winner->concrete_slot != kNoSlot) {
                services_[winner->concrete_slot].store(winner->concrete_ptr, std::memory_order_release);
            }
            owned_.push_back(std::move(winner->instance));
        }
        cs.entries.clear();
        cs.resolving = false;
        cs.pending.store(false, std::memory_order_release);
    }

    static inline std::atomic<int> next_slot_{0};

    std::array<std::atomic<void*>, Capacity> services_{};
    std::array<CandidateSlot, Capacity>      candidate_slots_{};
    std::vector<std::unique_ptr<Service>>    owned_;
    std::atomic<std::thread::id>             resolver_thread_{};
    bool                                     services_created_ = false;
};

}

#define SERVICELOCATOR_REGISTER(Locator, Type)                                        \
    [[maybe_unused]] static const bool servicelocator_registered_##Type =             \
        (Locator::AddServiceFactory([](Locator& locator) {                            \
             locator.AddCandidate<Type>(std::make_unique<Type>(locator), false);      \
         }), true)

#define SERVICELOCATOR_REGISTER_AS(Locator, Type, Base)                               \
    [[maybe_unused]] static const bool servicelocator_registered_##Type =             \
        (Locator::AddServiceFactory([](Locator& locator) {                            \
             locator.AddCandidateAlsoSelf<Type, Base>(std::make_unique<Type>(locator), \
                                                      false);                         \
         }), true)

#define SERVICELOCATOR_REGISTER_AS_FALLBACK(Locator, Type, Base)                      \
    [[maybe_unused]] static const bool servicelocator_registered_##Type =             \
        (Locator::AddServiceFactory([](Locator& locator) {                            \
             locator.AddCandidateAlsoSelf<Type, Base>(std::make_unique<Type>(locator), \
                                                      true);                          \
         }), true)
