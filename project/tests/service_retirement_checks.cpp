#include "engine/base/core/ServiceRetirementJournal.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
bool armed = false;
struct OwnedFailure {};
using Journal = Engine::Base::ServiceRetirementJournal<11>;
struct Service {
    Journal* journal = nullptr;
    std::size_t slot = 0;
    unsigned calls = 0;
    bool fail = false, recurse = false;
};
void Finalize(void* pointer) {
    auto& service = *static_cast<Service*>(pointer);
    ++service.calls;
    if (service.recurse) service.journal->Retire(service.slot);
    if (service.fail) throw OwnedFailure{};
}
}
void* operator new(std::size_t size) {
    if (armed) { armed = false; throw std::bad_alloc(); }
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    constexpr std::array<std::size_t, 11> order{0,10,9,8,7,6,4,5,1,3,2};
    for (std::size_t prefix = 0; prefix <= 11; ++prefix) {
        Journal journal;
        std::array<Service, 11> services{};
        armed = true;
        for (std::size_t slot = 0; slot < prefix; ++slot) {
            services[slot] = {&journal,slot,0,false,true};
            journal.Record(slot,&services[slot],Finalize);
        }
        assert(journal.PendingCount() == prefix);
        journal.Seal();
        for (const auto slot : order) journal.Retire(slot);
        for (const auto slot : order) journal.Retire(slot);
        assert(armed); armed = false;
        assert(journal.PendingCount() == 0);
        for (std::size_t slot = 0; slot < 11; ++slot)
            assert(services[slot].calls == (slot < prefix ? 1u : 0u));
    }
    Journal journal;
    Service first{&journal,0,0,true,true}, second{&journal,1};
    journal.Record(0,&first,Finalize); journal.Record(1,&second,Finalize);
    armed = true;
    bool original = false;
    try { journal.Retire(0); } catch (const OwnedFailure&) { original = true; }
    assert(original && journal.Contains(0) && journal.Contains(1) && first.calls == 1 && second.calls == 0);
    assert(armed); armed = false;
    first.fail = false;
    journal.Retire(0); journal.Retire(1);
    assert(journal.PendingCount() == 0 && first.calls == 2 && second.calls == 1);
    Journal invalid;
    unsigned refused = 0;
    try { invalid.Record(11,&first,Finalize); } catch (const std::invalid_argument&) { ++refused; }
    try { invalid.Record(0,nullptr,Finalize); } catch (const std::invalid_argument&) { ++refused; }
    try { invalid.Record(0,&first,nullptr); } catch (const std::invalid_argument&) { ++refused; }
    invalid.Record(0,&first,Finalize);
    try { invalid.Record(0,&second,Finalize); } catch (const std::logic_error&) { ++refused; }
    invalid.Seal();
    try { invalid.Record(1,&second,Finalize); } catch (const std::logic_error&) { ++refused; }
    try { invalid.Retire(11); } catch (const std::out_of_range&) { ++refused; }
    assert(refused == 6 && invalid.PendingCount() == 1);
    std::cout << "PASS: twelve acquisition prefixes, exact once/reentrant/no-allocation retirement, original failure/retry and invalid acquisition guards\n";
}
