// SPDX-License-Identifier: MIT
#define PRIME_TRACE_TEST 1
#include <aurora/frame_trace.hpp>
#include "../platform/ps4/frame_metrics.hpp"
#include <cassert>
#include <thread>
#include <iostream>
using namespace aurora::trace;
int main() {
    prime::FrameMetrics m;
    for(auto ns:{10000000,16666667,20000000,25000000,33333333,40000000}) m.add(ns);
    auto s=m.summary();assert(s.frames==6 && s.p50Ns==20000000 && s.p95Ns==40000000);
    assert(s.over1667==4 && s.over20==3 && s.over25==2 && s.over3333==1);
    m.reset();assert(m.summary().frames==0 && m.summary().over20==0);
    // Producer and consumer run concurrently; payload ordering must survive
    // publication and wrap-around. Full rings drop explicitly, never overwrite.
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for(uint64_t i=1;i<=200000;++i) emit({i,i*7,i*13,"test",Main,Phase});
        done.store(true,std::memory_order_release);
    });
    uint64_t seen=0,last=0;
    auto take=[&](const Event& e) { assert(e.frame>last);assert(e.startNs==e.frame*7);assert(e.value==e.frame*13);last=e.frame;++seen; };
    while(!done.load(std::memory_order_acquire)) drain(0,take);
    producer.join();drain(0,take);
    assert(seen+rings[0].dropped.load()==200000);
    cpu_clock=[]() noexcept -> uint64_t { static thread_local uint64_t cpu=0;return cpu+=100; };
    begin(Main,41);transition(Main,"input",now_ns());
    { Scope work("simulation"); }
    end(Main);
    unsigned scopes=0,complete=0,phases=0;
    drain(0,[&](const Event& e) { assert(e.frame==41);scopes+=e.kind==Inclusive;complete+=e.kind==Complete;phases+=e.kind==Phase; });
    assert(scopes>=1 && complete==1 && phases==2);
    // More than MaxThreads sequential worker lifetimes must reuse drained
    // retired rings rather than exhausting registrations after game restarts.
    for(unsigned n=0;n<32;++n) {
        std::thread worker([&] { emit({100+n,0,1,"lifecycle",Render,Counter}); });worker.join();
        unsigned count=0;
        for(unsigned i=0;i<MaxThreads;++i) drain(i,[&](const Event& e) { assert(e.frame==100+n);++count; });
        assert(count==1);
    }
    assert(unregisteredDrops.load()==0);
    std::cout<<"concurrent trace: "<<seen<<" published, "<<rings[0].dropped.load()<<" counted drops; frame identity and metrics verified\n";
}
