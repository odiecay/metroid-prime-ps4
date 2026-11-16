#include "draw_profile.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <type_traits>
using namespace aurora::gx::fifo::draw_profile;
static uint64_t wall=0,cpu=0,wallReads=0,cpuReads=0;
static uint64_t Wall() noexcept {++wallReads;return wall;}
static uint64_t Cpu() noexcept {++cpuReads;return cpu;}
static void reset(){wall=cpu=wallReads=cpuReads=0;}
static uint64_t RealWall() noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(){
 static_assert(std::is_trivially_copyable_v<Frame> && sizeof(Metrics)<256);
 Metrics off;reset();
 for(unsigned i=0;i<100000;++i){Draw d(off,false,Wall,Cpu);d.stage(Config);d.complete();}
 assert(!wallReads&&!cpuReads&&!off.frame.draws&&!off.frame.samples);
 Metrics rotating;unsigned seen[64]={};reset();
 for(unsigned frame=0;frame<64;++frame){
  for(unsigned i=0;i<64;++i){const auto old=wallReads;{Draw d(rotating,true,Wall,Cpu);d.stage(Arrays);d.stage(Arrays);d.complete();}if(wallReads!=old)++seen[i];}
  const auto f=rotating.take_frame();assert(f.draws==64&&f.samples==1&&!f.earlyReturns);
  assert(f.wallClockReads==3&&f.cpuClockReads==2&&f.cpuSamples==1);
 }
 for(auto n:seen)assert(n==1);
 Metrics partition;reset();
 {Draw d(partition,true,Wall,Cpu);wall=10;cpu=8;d.stage(Arrays);wall=30;cpu=24;d.stage(Pipeline);wall=50;cpu=40;d.complete();}
 auto f=partition.take_frame();assert(f.samples==1&&f.draws==1&&f.wallNs==50&&f.cpuNs==40);
 assert(f.stages[VertexIndex]==10&&f.stages[Arrays]==20&&f.stages[Pipeline]==20);
 assert(std::accumulate(f.stages.begin(),f.stages.end(),uint64_t{})==f.wallNs);
 Metrics early;reset();{Draw d(early,true,Wall,Cpu);wall=123;cpu=117;}
 f=early.take_frame();assert(f.earlyReturns==1&&f.wallNs==123&&f.cpuNs==117);
 Metrics unavailable;reset();cpu=Unavailable;{Draw d(unavailable,true,Wall,Cpu);wall=10;}
 f=unavailable.take_frame();assert(f.samples==1&&f.cpuClockReads==2&&!f.cpuSamples&&!f.cpuNs);
 Metrics missing;reset();{Draw d(missing,true,Wall);wall=10;d.complete();}
 f=missing.take_frame();assert(f.wallNs==10&&!f.cpuClockReads&&!f.cpuSamples);
 Metrics backward;reset();wall=100;cpu=50;{Draw d(backward,true,Wall,Cpu);wall=90;cpu=40;d.stage(Config);wall=80;d.complete();}
 f=backward.take_frame();assert(f.invalidWall&&f.cpuSamples==0&&f.cpuNs==0&&f.wallNs==0);
 // Boundary count: no clock call is made on any of the63 unsampled draws.
 Metrics count;reset();for(unsigned i=0;i<4096;++i){const auto before=wallReads;{Draw d(count,true,Wall,Cpu);d.stage(Config);d.stage(Pipeline);d.complete();}assert(wallReads-before==((i&63u)==0?4u:0u));}
 f=count.take_frame();assert(f.draws==4096&&f.samples==64&&f.wallClockReads==256&&f.cpuClockReads==128);
 // Observer cost is reported, never silently subtracted from measured work.
 for(bool enabled:{false,true}){
  Metrics metric;const auto start=RealWall();
  for(unsigned i=0;i<1000000;++i){Draw d(metric,enabled,RealWall);d.stage(Arrays);d.stage(Config);d.stage(Pipeline);d.stage(Textures);d.stage(BindGroups);d.stage(Uniforms);d.stage(Recording);d.complete();}
  const auto elapsed=RealWall()-start;const auto out=metric.take_frame();
  std::printf("DRAW_PROFILE_OBSERVER enabled=%d draws=1000000 wall_ms=%.3f sampled=%llu wall_clock_reads=%llu\n",int(enabled),elapsed/1e6,(unsigned long long)out.samples,(unsigned long long)out.wallClockReads);
 }
 std::puts("sampling disabled/unsampled clocks=0; rotation, partition, early returns, CPU availability and invalid clocks passed");
}
