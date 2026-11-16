// SPDX-License-Identifier: MIT
#include "webgpu/readback_state.hpp"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
using namespace aurora::webgpu::gpu_prof;
int main() {
    ReadbackState slot;
    assert(slot.load()==SlotState::Free);
    slot.reset(SlotState::Recording);
    slot=SlotState::InFlight;
    const auto old=slot.ticket();
    slot.reset(); // shutdown before the callback arrives
    slot.reset(SlotState::Recording);slot=SlotState::InFlight;
    const auto current=slot.ticket();
    assert(!slot.complete(old,true));assert(slot.load()==SlotState::InFlight);
    assert(slot.complete(current,true));assert(slot.load()==SlotState::Mapped);
    assert(!slot.complete(current,false)); // callbacks cannot publish twice
    slot=SlotState::Free;
    slot.reset(SlotState::Recording);slot=SlotState::InFlight;
    assert(slot.complete(slot.ticket(),false));assert(slot.load()==SlotState::Failed);

    // A delayed old completion races the owner's reset. Atomic generation and
    // phase must be tested together: separate load/store guards lose this race.
    for(unsigned n=0;n<10000;++n) {
        slot.reset(SlotState::InFlight);
        const auto ticket=slot.ticket();
        std::atomic<bool> go{false};
        std::thread callback([&] {
            while(!go.load(std::memory_order_acquire)) {}
            slot.complete(ticket,true);
        });
        go.store(true,std::memory_order_release);
        slot.reset(SlotState::InFlight);
        const auto newTicket=slot.ticket();
        callback.join();
        assert(slot.load()==SlotState::InFlight);
        assert(slot.complete(newTicket,true));
    }
    std::cout<<"10000 callback/reset races and stale-device, duplicate, failed completion cases passed\n";
}
