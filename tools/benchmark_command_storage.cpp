// SPDX-License-Identifier: MIT
// Host CPU storage microbenchmark. No renderer/GPU or PS4 FPS claim.
#include "gfx/command_pool.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <memory>
struct CommandPayload { std::array<uint64_t,16> words{}; };
static uint64_t allocated{},allocations{};
template<class T> struct Counted {
  using value_type=T;using is_always_equal=std::true_type;
  Counted()=default;template<class U> Counted(const Counted<U>&) {}
  T* allocate(size_t n) { ++allocations;allocated+=n*sizeof(T);return std::allocator<T>{}.allocate(n); }
  void deallocate(T* p,size_t n) { std::allocator<T>{}.deallocate(p,n); }
  template<class U> bool operator==(const Counted<U>&) const { return true; }
};
using Pool=aurora::gfx::detail::CommandStoragePool<CommandPayload,128,Counted<CommandPayload>>;
struct Result { uint64_t allocations,bytes,median,p95,retained,checksum; };
Result run(bool reuse,unsigned frames) {
  Pool pool(8u<<20);
  std::array<Pool::Vector,32> passes;
  std::vector<uint64_t> times;times.reserve(frames);
  uint64_t sum=0,oldAlloc=0,oldBytes=0;
  for(unsigned frame=0;frame<frames+100;++frame) {
    if(frame==100) {oldAlloc=allocations;oldBytes=allocated;}
    const auto start=std::chrono::steady_clock::now();
    for(unsigned pass=0;pass<passes.size();++pass) {
      passes[pass]=reuse?pool.take():Pool::Vector{};
      // Matches the new EFB starting capacity, changing only retained storage.
      if(passes[pass].capacity()<128) passes[pass].reserve(128);
      const unsigned count=pass==0?1600:16+(pass%4)*16;
      for(unsigned i=0;i<count;++i) {
        CommandPayload command{};command.words[0]=(uint64_t(frame)<<32)|i;
        passes[pass].push_back(command);
      }
      sum+=passes[pass].back().words[0];
    }
    for(auto& pass:passes) { if(reuse) pool.put(std::move(pass));else pass={}; }
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if(frame>=100) times.push_back(ns);
  }
  std::sort(times.begin(),times.end());
  return {allocations-oldAlloc,allocated-oldBytes,times[times.size()/2],times[(times.size()*95)/100],pool.retained_bytes(),sum};
}
int main() {
  std::puts("{\"scope\":\"Linux CPU microbenchmark; synthetic command payload, no PS4 FPS inference\",\"warmup_frames\":100,\"measured_frames\":2000,\"trials\":[");
  for(unsigned i=0;i<8;++i) {
    bool pooled=(i%4==0 || i%4==3); // ABBA order, avoid one monotonic warm-up bias
    const auto r=run(pooled,2000);
    std::printf("%s{\"reuse\":%s,\"allocations\":%llu,\"allocated_bytes\":%llu,\"median_ns\":%llu,\"p95_ns\":%llu,\"retained_bytes\":%llu,\"checksum\":%llu}",i?",\n":"",pooled?"true":"false",(unsigned long long)r.allocations,(unsigned long long)r.bytes,(unsigned long long)r.median,(unsigned long long)r.p95,(unsigned long long)r.retained,(unsigned long long)r.checksum);
  }
  std::puts("]}");
}
