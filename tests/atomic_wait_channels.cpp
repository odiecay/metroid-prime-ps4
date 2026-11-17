// SPDX-License-Identifier: MIT
// The actual native wait-channel implementation, exercised with host pthreads.
#include "../platform/ps4/compat/prime_atomic_wait.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

struct alignas(512) Cell { std::atomic<int> value{}; };
struct alignas(512) Cell64 { std::atomic<int64_t> value{}; };

int main() {
    Cell cells[2];
    assert(&prime::runtime::atomic_wait_bucket(&cells[0].value)==
           &prime::runtime::atomic_wait_bucket(&cells[1].value));
    // A notification before registration must not be lost.
    cells[0].value.store(1);
    prime::runtime::atomic_notify(&cells[0].value);
    prime::runtime::atomic_wait(&cells[0].value,[&] { return cells[0].value.load()==0; });
    cells[0].value.store(0);

    // Park another address's waiter first, in exactly the same hash bucket.
    // Waking just that waiter must not strand the intended address's waiter.
    std::atomic<bool> entered[2]{},finished[2]{};
    auto spawn=[&](int i) {
        return std::thread([&,i] {
            prime::runtime::atomic_wait(&cells[i].value,[&,i] {
                entered[i].store(true,std::memory_order_release);
                return cells[i].value.load(std::memory_order_acquire)==0;
            });
            finished[i].store(true,std::memory_order_release);
        });
    };
    auto other=spawn(1);
    while (!entered[1].load(std::memory_order_acquire)) std::this_thread::yield();
    auto intended=spawn(0);
    while (!entered[0].load(std::memory_order_acquire)) std::this_thread::yield();
    cells[0].value.store(1,std::memory_order_release);
    prime::runtime::atomic_notify(&cells[0].value);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!finished[0].load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    assert(finished[0].load());assert(!finished[1].load());
    cells[1].value.store(1,std::memory_order_release);
    prime::runtime::atomic_notify(&cells[1].value);
    intended.join();other.join();

    // Repeated opposing wait/notify races, beyond initial registration.
    std::atomic<int> turn{};
    std::atomic<unsigned> visits{};
    auto loop=[&](int own) {
        for (int i=0;i<4000;++i) {
            prime::runtime::atomic_wait(&turn,[&] { return turn.load(std::memory_order_acquire)!=own; });
            visits.fetch_add(1);
            turn.store(1-own,std::memory_order_release);
            prime::runtime::atomic_notify(&turn);
        }
    };
    std::thread first(loop,0),second(loop,1);first.join();second.join();
    assert(visits==8000);
    Cell64 wide[2];
    assert(&prime::runtime::atomic_wait_bucket(&wide[0].value)==
           &prime::runtime::atomic_wait_bucket(&wide[1].value));
    // Hold the real channel lock inside its equality predicate while the
    // producer changes only upper/sign bits, then let wait atomically register.
    // Notify must use this same address/channel and cannot slip into a gap.
    for (int64_t next:{int64_t(1)<<32,INT64_MIN,INT64_MAX}) {
        wide[0].value=0;
        std::atomic<bool> inside{},release{},first{true},done{},notifyStarted{};
        std::thread waiter([&] {
            prime::runtime::atomic_wait(&wide[0].value,[&] {
                const bool equal=wide[0].value.load(std::memory_order_acquire)==0;
                if (first.exchange(false)) {
                    inside.store(true,std::memory_order_release);
                    while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
                }
                return equal;
            });
            done=true;
        });
        while (!inside.load(std::memory_order_acquire)) std::this_thread::yield();
        wide[0].value.store(next,std::memory_order_release);
        std::thread notifier([&] {notifyStarted=true;prime::runtime::atomic_notify(&wide[0].value);});
        while (!notifyStarted.load()) std::this_thread::yield();
        assert(!done.load());release=true;notifier.join();waiter.join();assert(done.load());
    }
    puts("Signed64 native channels: upper32/sign bits and3 locked-registration races passed");
    puts("Wait channels: early notify, colliding addresses and 8000 handoffs passed");
}
