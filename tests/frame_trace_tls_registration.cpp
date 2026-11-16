#define PRIME_TRACE_TEST 1
#include <aurora/frame_trace.hpp>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace aurora::trace;
static std::atomic<unsigned> observations{0};
static void Observe(const void* address,unsigned slot) noexcept {
  assert(address && slot<MaxThreads);
  assert(tls_template_canary==0x5052544c);
  assert(rings[slot].owner.load(std::memory_order_acquire)==1);
  observations.fetch_add(1,std::memory_order_relaxed);
}
static void EmptyRetired() {
  for(unsigned i=0;i<MaxThreads;++i) drain(i,[](const Event&) {});
}
int main() {
  registration_observer=Observe;
  // Exact old failure: zero initialization used to mean already registered as
  // slot zero, without any ownership CAS. This uses the production type/API.
  {
    Registration zero;
    std::memset(&zero,0,sizeof(zero));
    const unsigned slot=zero.acquire();
    assert(slot<MaxThreads && rings[slot].owner.load()==1);
    assert(zero.acquire()==slot && observations.load()==1);
  }
  EmptyRetired();
  // A failed claim must not retire somebody else's ring on destruction.
  {
    std::array<std::unique_ptr<Registration>,MaxThreads> held;
    for(unsigned i=0;i<MaxThreads;++i) {
      held[i]=std::make_unique<Registration>();
      assert(held[i]->acquire()==i);
    }
    { Registration overflow; assert(overflow.acquire()==MaxThreads); }
    for(const auto& ring:rings) assert(ring.owner.load()==1);
    held[3].reset();
    assert(rings[3].owner.load()==2);
    drain(3,[](const Event&) {});
    Registration replacement;
    assert(replacement.acquire()==3);
  }
  EmptyRetired();
  constexpr unsigned PerThread=20000;
  std::array<std::atomic<uint64_t>,MaxThreads> seen{};
  std::array<uint64_t,MaxThreads> last{};
  std::atomic<unsigned> ready{0},done{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> workers;
  for(unsigned producer=0;producer<MaxThreads;++producer) workers.emplace_back([&,producer] {
    ready.fetch_add(1,std::memory_order_release);
    while(!go.load(std::memory_order_acquire)) std::this_thread::yield();
    for(uint64_t n=1;n<=PerThread;++n)
      emit({producer+1,n,n*13,"unique concurrent producer",producer&1?Render:Main,Counter});
    done.fetch_add(1,std::memory_order_release);
  });
  while(ready.load(std::memory_order_acquire)!=MaxThreads) std::this_thread::yield();
  go.store(true,std::memory_order_release);
  const auto consume=[&](const Event& event) {
    assert(event.frame>=1 && event.frame<=MaxThreads);
    const unsigned producer=event.frame-1;
    assert(event.kind==Counter && event.lane==(producer&1?Render:Main));
    assert(std::strcmp(event.name,"unique concurrent producer")==0);
    assert(event.startNs>last[producer] && event.value==event.startNs*13);
    last[producer]=event.startNs;
    ++seen[producer];
  };
  while(done.load(std::memory_order_acquire)!=MaxThreads)
    for(unsigned i=0;i<MaxThreads;++i) drain(i,consume);
  for(auto& thread:workers) thread.join();
  for(unsigned i=0;i<MaxThreads;++i) drain(i,consume);
  uint64_t delivered=0,dropped=unregisteredDrops.load();
  for(unsigned i=0;i<MaxThreads;++i) {
    delivered+=seen[i].load();dropped+=rings[i].dropped.load();
    assert(rings[i].owner.load()==0);
  }
  assert(delivered+dropped==uint64_t(MaxThreads)*PerThread);
  // More than the ring count sequential TLS lifetimes must reuse retired slots.
  for(unsigned n=0;n<64;++n) {
    std::thread worker([&] { emit({100+n,0,1,"retired reuse",Main,Counter}); });
    worker.join();unsigned count=0;
    for(unsigned i=0;i<MaxThreads;++i) drain(i,[&](const Event& e) {
      assert(e.frame==100+n);++count;
    });
    assert(count==1);
  }
  assert(unregisteredDrops.load()==0);
  registration_observer=nullptr;
  std::printf("PASS: actual zeroed registration claims ownership;8 concurrent producers,%llu delivered+%llu bounded drops;64 retired TLS reuses\n",
              static_cast<unsigned long long>(delivered),static_cast<unsigned long long>(dropped));
}
