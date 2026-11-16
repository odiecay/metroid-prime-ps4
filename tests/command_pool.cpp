#include "gfx/command_pool.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <thread>

namespace {
std::atomic<size_t> allocations{}, deallocations{};
std::atomic<bool> failNext{};
template<class T> struct CountedAllocator {
  using value_type = T;
  using is_always_equal = std::true_type;
  CountedAllocator() noexcept = default;
  template<class U> CountedAllocator(const CountedAllocator<U>&) noexcept {}
  T* allocate(size_t count) {
    if(failNext.exchange(false)) throw std::bad_alloc{};
    ++allocations;return std::allocator<T>{}.allocate(count);
  }
  void deallocate(T* ptr,size_t count) noexcept { ++deallocations;std::allocator<T>{}.deallocate(ptr,count); }
  template<class U> bool operator==(const CountedAllocator<U>&) const noexcept { return true; }
};
struct Payload { std::array<uint64_t,16> words{};std::shared_ptr<int> owner; };
using Pool=aurora::gfx::detail::CommandStoragePool<Payload,128,CountedAllocator<Payload>>;
using Vector=Pool::Vector;
}

int main() {
  {
    Pool pool(1024*1024);
    Vector a;a.reserve(32);auto* address=a.data();
    auto owned=std::make_shared<int>(7);std::weak_ptr<int> weak=owned;
    a.push_back(Payload{.owner=std::move(owned)});
    pool.put(std::move(a));assert(weak.expired()); // No command/resource is cached.
    auto again=pool.take();assert(again.empty() && again.data()==address);
    pool.put(std::move(again));pool.clear();assert(pool.retained_bytes()==0);
  }
  {
    Pool pool;
    Vector values;values.reserve(32);values.resize(8);
    auto* original=values.data();const auto capacity=values.capacity();
    failNext.store(true);
    bool failed=false;
    try {values.resize(capacity+1);} catch(const std::bad_alloc&) {failed=true;}
    assert(failed && values.data()==original && values.size()==8);
    pool.put(std::move(values));auto recovered=pool.take();
    assert(recovered.empty() && recovered.data()==original);
  }
  {
    aurora::gfx::detail::CommandStoragePool<int,2> pool(64);
    for(unsigned i=0;i<10;++i) { std::vector<int> v(8,int(i));pool.put(std::move(v)); }
    assert(pool.size()==2 && pool.retained_bytes()==64);
    auto first=pool.take(),second=pool.take();
    assert(first.empty() && second.empty() && pool.retained_bytes()==0);
    std::vector<int> oversized(1024);pool.put(std::move(oversized));
    assert(pool.size()==0 && pool.retained_bytes()==0);
  }
  // A representative mixture of large/small passes warms once, then reuses
  // every vector without further heap allocations over 2000 frames.
  const size_t before=allocations;
  size_t warm=0;
  {
    Pool pool(8u<<20);
    std::array<Vector,32> passes;
    for(unsigned frame=0;frame<2000;++frame) {
      for(unsigned pass=0;pass<passes.size();++pass) {
        passes[pass]=pool.take();
        const size_t count=pass==0?1600:16+(pass%4)*16;
        for(size_t i=0;i<count;++i) {
          Payload value{};value.words[0]=(uint64_t(frame)<<32)|i;
          passes[pass].push_back(std::move(value));
        }
      }
      for(auto& pass:passes) {
        assert(pass.front().words[0]>>32==frame);
        pool.put(std::move(pass));
      }
      assert(pool.retained_bytes()<=pool.byte_limit());
      if(frame==0) warm=allocations;
      else assert(allocations==warm);
    }
  }
  assert(allocations==deallocations);
  // Same-slot handoff between recording and encoding threads: no storage may
  // be borrowed until the encoder explicitly releases its completed frame.
  {
    Pool pool;
    Vector recording;
    std::atomic<unsigned> turn{0};
    std::thread consumer([&] {
      for(unsigned frame=0;frame<10000;++frame) {
        while(turn.load(std::memory_order_acquire)!=1) std::this_thread::yield();
        assert(recording.size()==128 && recording.back().words[0]==frame);
        pool.put(std::move(recording));
        turn.store(0,std::memory_order_release);
      }
    });
    for(unsigned frame=0;frame<10000;++frame) {
      while(turn.load(std::memory_order_acquire)!=0) std::this_thread::yield();
      recording=pool.take();assert(recording.empty());
      recording.resize(128);recording.back().words[0]=frame;
      turn.store(1,std::memory_order_release);
    }
    consumer.join();
  }
  assert(allocations==deallocations);
  std::printf("Command pool: 2000 warmed frames with zero new vector allocations; %zu warmup allocations; bounded retention, ownership release and 10000 thread handoffs passed\n",warm-before);
}
