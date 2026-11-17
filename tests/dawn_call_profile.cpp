#include "prime_dawn_call_profile.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>
#include <new>
static bool trackAllocations=false;
static size_t allocationCount=0;
void* operator new(size_t n){if(trackAllocations)++allocationCount;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}

using namespace prime::dawn_call_profile;
static_assert(unsigned(Category::Draw)==0&&unsigned(Category::Descriptor)==1&&
              unsigned(Category::Pipeline)==2&&unsigned(Category::Barrier)==3&&
              unsigned(Category::PushConstants)==4&&unsigned(Category::DynamicState)==5&&CategoryCount==6);
static_assert(sizeof(PrimeDawnCallStats)==5*sizeof(uint64_t));
using Snapshot=std::array<PrimeDawnCallStats,CategoryCount>;
static thread_local std::vector<Snapshot> observed;
static thread_local uint64_t clockReads=0,clockValue=0,operations=0,argumentHash=0;
static thread_local bool badClock=false,absentClock=false;
static uint64_t clock_fake() noexcept {
    ++clockReads;
    if(absentClock)return UINT64_MAX;
    if(badClock)return clockReads&1?100:50;
    return clockValue+=10;
}
static void operation(uint64_t a,uint32_t b,uint32_t c) noexcept {
    ++operations;argumentHash=argumentHash*31+a+b*3+c*7;
}
static void noop_observer(const PrimeDawnCallStats*,unsigned) {}
struct Viewport { float x,y,width,height,minDepth,maxDepth; };
static uint64_t shapedCalls=0;
static const void* expectedBytes=nullptr;
static const Viewport* expectedViewports=nullptr;
static void push_constants(uint64_t command,uint64_t layout,uint32_t stages,uint32_t offset,
                           uint32_t size,const void* bytes) noexcept {
    assert(command==17&&layout==29&&stages==7&&offset==12&&size==16&&bytes==expectedBytes);
    const auto* words=static_cast<const uint32_t*>(bytes);assert(words[0]==0x12345678&&words[3]==0xfedcba98);++shapedCalls;
}
static void dynamic_state(uint64_t command,uint32_t first,uint32_t count,const Viewport* viewports) noexcept {
    assert(command==17&&first==2&&count==2&&viewports==expectedViewports);
    assert(viewports[0].height==-1080.f&&viewports[1].maxDepth==.75f);++shapedCalls;
}
extern "C" void external_profile_call();
#ifndef NO_OBSERVER
extern "C" void prime_ps4_dawn_call_stats(const PrimeDawnCallStats* rows,unsigned count) {
    assert(count==CategoryCount);
    assert(currentBatch==nullptr); // the observer is outside all measured work
    Snapshot copy;std::copy_n(rows,count,copy.begin());observed.push_back(copy);
    // A callback that uses an instrumented helper cannot recursively profile itself.
    call(Category::Barrier,operation,11,12,13);
}
#endif
static_assert(!std::is_copy_constructible_v<Batch> && !std::is_move_constructible_v<Batch>);
static void early_return() {
    Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);
    call(Category::Draw,operation,1,2,3);
    return;
}
int main() {
    const uint32_t words[]={0x12345678,1,2,0xfedcba98};expectedBytes=words;
    const Viewport viewports[]={{0,1080,1920,-1080,0,1},{1,2,3,4,.25f,.75f}};expectedViewports=viewports;
    for(bool enabled:{false,true}) {
        shapedCalls=0;
        {Batch batch(enabled,noop_observer,clock_fake);
         for(unsigned i=0;i<10000;++i) {
             call(Category::PushConstants,push_constants,17ull,29ull,7u,12u,16u,words);
             call(Category::DynamicState,dynamic_state,17ull,2u,2u,viewports);
         }}
        assert(shapedCalls==20000);
    }
    // The actual Vulkan-shaped helper must preserve each argument and invoke
    // the driver exactly once in both observed and disabled modes.
    for(unsigned i=0;i<10000;++i)operation(i,2,3);
    const auto reference=argumentHash;
    for(bool enabled:{false,true}) {
        argumentHash=0;operations=0;clockReads=0;
        {Batch batch(enabled,noop_observer,clock_fake);
         for(unsigned i=0;i<10000;++i)call(Category::Draw,operation,i,2,3);}
        assert(argumentHash==reference&&operations==10000);
    }
    allocationCount=0;trackAllocations=true;
    {Batch batch(true,noop_observer,clock_fake);for(unsigned i=0;i<100000;++i)call(Category::Draw,operation,i,2,3);}
    trackAllocations=false;assert(allocationCount==0);
    observed.clear();clockReads=0;operations=0;argumentHash=0;
    {
        Batch batch(false,prime_ps4_dawn_call_stats,clock_fake);
        for(unsigned i=0;i<10000;++i)call(Category::Draw,operation,i,2,3);
    }
    assert(observed.empty()&&clockReads==0&&operations==10000&&currentBatch==nullptr);
#ifdef NO_OBSERVER
    {Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);call(Category::Draw,operation,1,2,3);}
    assert(observed.empty()&&clockReads==0&&operations==10001&&currentBatch==nullptr);
    std::puts("absent_observer=1 zero_clock_reads=1");return 0;
#else
    nextBatch=0;clockReads=0;
    {Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);
     for(unsigned i=0;i<100000;++i)for(unsigned c=0;c<CategoryCount;++c)
         call(Category(c),operation,i,c,9);
     assert(observed.empty());}
    assert(observed.size()==1&&currentBatch==nullptr);
    for(unsigned c=0;c<CategoryCount;++c){const auto s=observed[0][c];const auto eligible=(99999u-((c*17u)&63u))/64u+1u;
        assert(s.calls==100000&&s.samples==64&&s.sampledWallNs==640&&!s.invalidClocks&&s.cappedSamples==eligible-64); }
    assert(clockReads==CategoryCount*64*2);
    observed.clear();nextBatch=0;clockReads=0;
    for(unsigned b=0;b<64;++b) {
        Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);
        for(unsigned c=0;c<CategoryCount;++c)call(Category(c),operation,b,c,5);
    }
    assert(observed.size()==64);
    for(unsigned c=0;c<CategoryCount;++c){uint64_t samples=0;for(auto row:observed){assert(row[c].calls==1);samples+=row[c].samples;}assert(samples==1);}
    assert(clockReads==2*CategoryCount);
    observed.clear();nextBatch=0;
    {
        Batch outer(true,prime_ps4_dawn_call_stats,clock_fake);
        call(Category::Draw,operation,1,2,3);
        {Batch paused(false,prime_ps4_dawn_call_stats,clock_fake);call(Category::Draw,operation,1,2,3);}
        {Batch inner(true,prime_ps4_dawn_call_stats,clock_fake);call(Category::Descriptor,operation,1,2,3);
         call(Category::PushConstants,push_constants,17ull,29ull,7u,12u,16u,words);}
        assert(observed.size()==1&&observed[0][1].calls==1);
        assert(observed[0][4].calls==1);
        external_profile_call();
        call(Category::DynamicState,dynamic_state,17ull,2u,2u,viewports);
    }
    assert(observed.size()==2&&observed[1][0].calls==1&&observed[1][1].calls==1);
    assert(observed[1][4].calls==0&&observed[1][5].calls==1);
    assert(currentBatch==nullptr);
    observed.clear();nextBatch=0;badClock=true;clockReads=0;
    {Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);call(Category::Draw,operation,1,2,3);}
    assert(observed[0][0].samples==0&&observed[0][0].invalidClocks==1&&observed[0][0].sampledWallNs==0);
    observed.clear();nextBatch=0;badClock=false;absentClock=true;
    {Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);call(Category::Draw,operation,1,2,3);}
    assert(observed[0][0].samples==0&&observed[0][0].invalidClocks==1);
    absentClock=false;observed.clear();nextBatch=0;
    early_return();assert(observed.size()==1&&observed[0][0].calls==1);
    observed.clear();
    std::array<uint64_t,2> workerCounts{};
    std::thread a([&]{observed.clear();for(unsigned b=0;b<40;++b){Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);for(unsigned i=0;i<100;++i)external_profile_call();}for(auto r:observed)workerCounts[0]+=r[1].calls;assert(currentBatch==nullptr);});
    std::thread b([&]{observed.clear();for(unsigned b=0;b<40;++b){Batch batch(true,prime_ps4_dawn_call_stats,clock_fake);for(unsigned i=0;i<100;++i)external_profile_call();}for(auto r:observed)workerCounts[1]+=r[1].calls;assert(currentBatch==nullptr);});
    a.join();b.join();assert(workerCounts[0]==4000&&workerCounts[1]==4000&&observed.empty());
    std::printf("profiled_calls=%u capped_samples=%u rotated_batches=64 nested_early_returns=1 cross_tu_tls=1 threads=2 invalid_clock=1 new_vulkan_arguments=40000 old_ids_preserved=1\n",CategoryCount*100000,CategoryCount*64);
#endif
}
