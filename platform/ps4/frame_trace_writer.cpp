// SPDX-License-Identifier: MIT
#include <aurora/frame_trace.hpp>
#include "frame_metrics.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <string>
#include <chrono>
#ifdef __ORBIS__
#include "disc/gc_iso_cache.h"
#include <aurora/dvd.h>
#endif

namespace {
using namespace aurora::trace;
constexpr unsigned MaxStages=128, Buckets=1024;
struct Stage { const char* name{}; Kind kind{}; Lane lane{}; };
struct Bucket {
    uint64_t frame{};
    bool complete{};
    std::array<uint64_t,MaxStages> values{};
};
struct Accum { prime::FrameMetrics times; uint64_t counter{},unattributed{},events{}; };
std::array<Stage,MaxStages> stages;
std::array<std::array<Bucket,Buckets>,MaxThreads> buckets;
unsigned stageCount=0;
uint64_t collisions=0, unknownStages=0;
struct SlowFrame { unsigned thread{};uint64_t key{},wall{};std::array<uint64_t,MaxStages> values{}; };
std::array<SlowFrame,16> slow{};
unsigned slowCount=0;
uint64_t slowOmitted=0;
unsigned stage_index(const Event& e) {
    for(unsigned i=0;i<stageCount;++i)
        if(stages[i].kind==e.kind && stages[i].lane==e.lane && !std::strcmp(stages[i].name,e.name)) return i;
    if(stageCount==MaxStages) { ++unknownStages;return MaxStages; }
    stages[stageCount]={e.name,e.kind,e.lane};return stageCount++;
}
std::array<std::array<Accum,MaxStages>,MaxThreads> accum;
void finish_bucket(unsigned thread,Bucket& b) {
    uint64_t wall=0;
    for(unsigned s=0;s<stageCount;++s) if(b.values[s]) {
        if(stages[s].kind!=Counter) accum[thread][s].times.add(b.values[s]);
        if(stages[s].kind==Complete) wall=b.values[s];
    }
    if(wall>20000000) {
        unsigned at=0;
        while(at<slowCount && slow[at].wall>=wall) ++at;
        if(slowCount==slow.size()) ++slowOmitted;
        if(at<slow.size()) {
            const unsigned last=std::min<unsigned>(slowCount,slow.size()-1);
            for(unsigned k=last;k>at;--k) slow[k]=slow[k-1];
            slow[at]={thread,b.frame,wall,b.values};
            slowCount=std::min<unsigned>(slowCount+1,slow.size());
        }
    }
    b={};
}
void consume(unsigned thread,const Event& e) {
    const auto stage=stage_index(e);
    if(stage==MaxStages) return;
    auto& a=accum[thread][stage];++a.events;
    if(e.kind==Gpu) { a.times.add(e.value);return; }
    if(e.kind==Counter) { a.counter+=e.value;if(!e.frame) return; }
    if(!e.frame) { a.unattributed+=e.value;return; }
    // Distinct logical lanes can run inline on the same producer. Include the
    // lane in the key rather than combining main and worker frame lifetimes.
    auto& b=buckets[thread][(e.frame*2+unsigned(e.lane))%Buckets];
    const auto key=e.frame*2+unsigned(e.lane);
    if(b.frame && b.frame!=key) { ++collisions;b={}; }
    b.frame=key;b.values[stage]+=e.value;
    // Complete is the producer's final event for this frame/lane. Aggregate
    // now, rather than retaining completed buckets for five seconds: a short
    // uncapped/loading burst can otherwise wrap the frame-key table and lose
    // data despite the SPSC rings having no drops. This runs only on the
    // diagnostic consumer; producer/GPU ownership and timing are unchanged.
    if(e.kind==Complete) finish_bucket(thread,b);
}
void json_name(FILE* out,const char* text) {
    std::fputc('"',out);
    for(;*text;++text) {
        if(*text=='"' || *text=='\\') std::fputc('\\',out);
        if(static_cast<unsigned char>(*text)>=32) std::fputc(*text,out);
    }
    std::fputc('"',out);
}
}
static void poll_trace(uint64_t now,bool force=false) {
    using namespace aurora::trace;
    static uint64_t start=0;
    if(!start) start=now;
    for(unsigned t=0;t<MaxThreads;++t) drain(t,[t](const Event& e) { consume(t,e); });
    if(!force && now-start<5000000000ull) return;
    static FILE* out=nullptr;
    static const std::string outputPath=[] {
        const char* path=std::getenv("MP_PS4_PROFILE_LOG");
        return std::string(path?path:"/data/project-prime/logs/frame-profile.jsonl");
    }();
    static bool failed=false;
    // The file appends across launches, while Orbis monotonic time restarts.
    // Keep a stable ID across this process's rotations so analysis never pools
    // repeated frame IDs or mistakes a previous launch for fresh telemetry.
    static const std::string sessionId=[] {
        const auto wall=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return std::to_string(wall)+"-"+std::to_string(aurora::trace::now_ns());
    }();
    if(!out && !failed) {
        out=std::fopen(outputPath.c_str(),"a");
        if(!out) { failed=true;std::fprintf(stderr,"orbis: frame profiler output unavailable\n"); }
    }
    if(out) {
        uint64_t dropped=unregisteredDrops.exchange(0,std::memory_order_relaxed);
        for(auto& ring:rings) dropped+=ring.dropped.exchange(0,std::memory_order_relaxed);
        std::fprintf(out,"{\"schema\":1,\"type\":\"cpu_frame_window\",\"session_id\":\"%s\",\"start_ns\":%llu,\"end_ns\":%llu,"
            "\"dropped_events\":%llu,\"bucket_collisions\":%llu,\"unknown_stages\":%llu,"
            "\"timing\":\"monotonic wall; inclusive rows overlap; waits are not CPU execution\",\"stages\":[",
            sessionId.c_str(),(unsigned long long)start,(unsigned long long)now,(unsigned long long)dropped,
            (unsigned long long)collisions,(unsigned long long)unknownStages);
        bool first=true;
        for(unsigned t=0;t<MaxThreads;++t) for(unsigned i=0;i<stageCount;++i) {
            const auto& a=accum[t][i];const auto s=a.times.summary();
            if(!a.events && !s.frames && !a.unattributed) continue;
            if(!first) std::fputc(',',out);first=false;
            std::fprintf(out,"{\"thread\":%u,\"lane\":\"%s\",\"stage\":",t,stages[i].lane==Main?"main":"render");
            json_name(out,stages[i].name);
            std::fprintf(out,",\"kind\":%u,\"frames\":%llu,\"sum_ns\":%llu,\"median_ns\":%llu,"
                "\"p95_ns\":%llu,\"p99_ns\":%llu,\"max_ns\":%llu,\"counter\":%llu,\"unattributed_ns\":%llu,"
                "\"over_1667\":%llu,\"over_20\":%llu,\"over_25\":%llu,\"over_3333\":%llu}",
                unsigned(stages[i].kind),(unsigned long long)s.frames,(unsigned long long)s.sumNs,
                (unsigned long long)s.p50Ns,(unsigned long long)s.p95Ns,(unsigned long long)s.p99Ns,
                (unsigned long long)s.worstNs,(unsigned long long)a.counter,(unsigned long long)a.unattributed,
                (unsigned long long)s.over1667,(unsigned long long)s.over20,
                (unsigned long long)s.over25,(unsigned long long)s.over3333);
        }
        std::fprintf(out,"],\"slow_frames_omitted\":%llu,\"slow_frames\":[",(unsigned long long)slowOmitted);
        for(unsigned n=0;n<slowCount;++n) {
            const auto& f=slow[n];
            std::fprintf(out,"%s{\"frame_id\":%llu,\"thread\":%u,\"lane\":\"%s\",\"wall_ns\":%llu,\"stages\":[",
                n?",":"",(unsigned long long)(f.key/2),f.thread,(f.key&1)?"render":"main",(unsigned long long)f.wall);
            bool firstStage=true;
            for(unsigned s=0;s<stageCount;++s) if(f.values[s]) {
                if(!firstStage) std::fputc(',',out);firstStage=false;
                std::fputs("{\"stage\":",out);json_name(out,stages[s].name);
                std::fprintf(out,",\"kind\":%u,\"value\":%llu}",unsigned(stages[s].kind),(unsigned long long)f.values[s]);
            }
            std::fputs("]}",out);
        }
        std::fputs("]",out);
#ifdef __ORBIS__
        PrimeIsoReadCacheMetrics discCache{};
        prime_iso_read_cache_metrics(&discCache);
        std::fprintf(out,",\"disc_read_cache\":{\"block_hits\":%llu,\"block_misses\":%llu,"
            "\"source_read_calls\":%llu,\"source_read_bytes\":%llu,\"retained_bytes\":%llu,"
            "\"exact_fallbacks\":%llu,\"bulk_bypass_requests\":%llu}",
            (unsigned long long)discCache.block_hits,(unsigned long long)discCache.block_misses,
            (unsigned long long)discCache.source_read_calls,(unsigned long long)discCache.source_read_bytes,
            (unsigned long long)discCache.retained_bytes,(unsigned long long)discCache.exact_fallbacks,
            (unsigned long long)discCache.bulk_bypass_requests);
        std::fprintf(stdout,"orbis: original-disc read cache: hits=%llu misses=%llu source_calls=%llu "
            "source_bytes=%llu retained_KiB=%llu fallback=%llu bulk=%llu (process totals)\n",
            (unsigned long long)discCache.block_hits,(unsigned long long)discCache.block_misses,
            (unsigned long long)discCache.source_read_calls,(unsigned long long)discCache.source_read_bytes,
            (unsigned long long)(discCache.retained_bytes/1024),(unsigned long long)discCache.exact_fallbacks,
            (unsigned long long)discCache.bulk_bypass_requests);
        AuroraDvdWaitMetrics dvdWait{};
        aurora_dvd_wait_metrics(&dvdWait);
        std::fprintf(out,",\"dvd_wait\":{\"samples\":%llu,\"untracked\":%llu,\"waiting\":%llu,"
            "\"wait_ns\":%llu,\"queue_ns\":%llu,\"service_ns\":%llu,\"callback_ns\":%llu,\"other_ns\":%llu,"
            "\"max_wait_ns\":%llu,\"worst_queue_ns\":%llu,\"worst_service_ns\":%llu,"
            "\"worst_callback_ns\":%llu,\"worst_other_ns\":%llu,\"worst_length\":%u,\"worst_offset\":%u,"
            "\"worst_entry\":%d,\"worst_priority\":%d,\"worst_overlay\":%s}",
            (unsigned long long)dvdWait.samples,(unsigned long long)dvdWait.untracked,(unsigned long long)dvdWait.waiting,
            (unsigned long long)dvdWait.wait_ns,(unsigned long long)dvdWait.queue_ns,(unsigned long long)dvdWait.service_ns,
            (unsigned long long)dvdWait.callback_ns,(unsigned long long)dvdWait.other_ns,(unsigned long long)dvdWait.max_wait_ns,
            (unsigned long long)dvdWait.worst_queue_ns,(unsigned long long)dvdWait.worst_service_ns,
            (unsigned long long)dvdWait.worst_callback_ns,(unsigned long long)dvdWait.worst_other_ns,
            dvdWait.worst_length,dvdWait.worst_offset,dvdWait.worst_entry,dvdWait.worst_priority,dvdWait.worst_overlay?"true":"false");
        std::fprintf(stdout,"orbis: mandatory-read waits: samples=%llu untracked=%llu active=%llu worst=%.3f ms "
            "(queue %.3f, service %.3f, callback %.3f, wake/other %.3f) entry=%d offset=%u bytes=%u overlay=%d\n",
            (unsigned long long)dvdWait.samples,(unsigned long long)dvdWait.untracked,(unsigned long long)dvdWait.waiting,
            dvdWait.max_wait_ns/1e6,dvdWait.worst_queue_ns/1e6,dvdWait.worst_service_ns/1e6,
            dvdWait.worst_callback_ns/1e6,dvdWait.worst_other_ns/1e6,
            dvdWait.worst_entry,dvdWait.worst_offset,dvdWait.worst_length,int(dvdWait.worst_overlay));
#endif
        std::fputs("}\n",out);std::fflush(out);
        // Bounded diagnostics storage, rotated only by its consumer thread.
        if(std::ftell(out)>=16*1024*1024) {
            std::fclose(out);out=nullptr;
            std::remove((outputPath+".previous").c_str());
            if(std::rename(outputPath.c_str(),(outputPath+".previous").c_str())) failed=true;
        } else if(std::ferror(out)) {
            std::fclose(out);out=nullptr;failed=true;
            std::fprintf(stderr,"orbis: frame profiler stopped writing after storage failure\n");
        }
    }
    for(auto& thread:accum) for(auto& a:thread) a={};
    slowCount=0;slowOmitted=0;
    start=now;collisions=unknownStages=0;
}

#ifdef __ORBIS__
// SDK declares this native function without parameters. Keep its verified BSD
// signature local rather than applying the SDK's Linux clock ID 3 to Orbis.
struct PrimeNativeTimespec { int64_t seconds,nanoseconds; };
extern "C" int prime_native_clock_gettime(int,PrimeNativeTimespec*) asm("sceKernelClockGettime");
extern "C" void* prime_native_pthread_self() asm("scePthreadSelf");
static void trace_registration_observed(const void* address,unsigned slot) noexcept {
    // Once per acquired ring, never per event/draw. Native identities make a
    // zero-initialized TLS template or shared TLS storage visible in boot logs.
    std::fprintf(stderr,"orbis: trace registration: slot=%u tls=%p thread=%p template=%08x expected=5052544c\n",
                 slot,address,prime_native_pthread_self(),
                 unsigned(aurora::trace::tls_template_canary));
}
static uint64_t thread_cpu_ns() noexcept {
    static thread_local bool unsupported=false;
    if(unsupported) return UINT64_MAX;
    PrimeNativeTimespec t{-1,-1};
    if(prime_native_clock_gettime(14,&t)!=0 || t.seconds<0 || t.nanoseconds<0 || t.nanoseconds>=1000000000) {
        unsupported=true;return UINT64_MAX;
    }
    return uint64_t(t.seconds)*1000000000ull+uint64_t(t.nanoseconds);
}
#endif
namespace {
std::atomic<bool> traceStop{false};
std::thread traceWorker;
}
extern "C" void prime_ps4_driver_duration(unsigned,uint64_t);
extern "C" { extern void (*prime_orbis_profile_callback)(unsigned,uint64_t); }
extern "C" void prime_ps4_trace_start() {
    prime_orbis_profile_callback=prime_ps4_driver_duration;
#ifdef __ORBIS__
    aurora::trace::cpu_clock=thread_cpu_ns;
    aurora::trace::registration_observer=trace_registration_observed;
#endif
    try {
        traceWorker=std::thread([] {
            while(!traceStop.load(std::memory_order_acquire)) {
                poll_trace(aurora::trace::now_ns());
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            poll_trace(aurora::trace::now_ns(),true);
        });
    } catch(...) { std::fprintf(stderr,"orbis: frame profiler thread unavailable; tracing disabled by bounded drops\n"); }
}
extern "C" void prime_ps4_trace_stop() {
    traceStop.store(true,std::memory_order_release);
    if(traceWorker.joinable()) traceWorker.join();
}

extern "C" void prime_ps4_driver_duration(unsigned id,uint64_t ns) {
    static constexpr const char* names[]={"native queue submit (inclusive)","submit buffer preparation (inclusive)",
        "WSI presentation (inclusive)","WSI GPU completion wait (inclusive)","scanout copy (inclusive)","video-out flip queue wait (inclusive)"};
    if(id<45 || id>50) return;
    const auto now=aurora::trace::now_ns();
    aurora::trace::emit({aurora::trace::frame_id(aurora::trace::Render),now>=ns?now-ns:0,ns,
        names[id-45],aurora::trace::Render,aurora::trace::Inclusive});
}
extern "C" void prime_ps4_dawn_duration(unsigned id,uint64_t ns) {
    using namespace aurora::trace;
    static constexpr const char* names[]={"Dawn Vulkan command translation (inclusive)",
        "Dawn device tick (inclusive)","Dawn pending command submission (inclusive)",
        "Dawn resource preparation (inclusive)","Dawn render-pass replay (inclusive)",
        "Dawn render-pass setup (inclusive)"};
    if(id>=std::size(names)) return;
    const auto now=now_ns();
    emit({frame_id(Render),now>=ns?now-ns:0,ns,names[id],Render,Inclusive});
}
extern "C" void prime_ps4_descriptor_bind_counts(uint64_t calls,uint64_t sets) {
    // Dawn aggregates inside each descriptor tracker. Two events per render or
    // compute pass, rather than two events per draw, keep tracing inexpensive.
    aurora::trace::count("Dawn tracker descriptor bind calls",calls,aurora::trace::Render);
    aurora::trace::count("Dawn tracker descriptor sets",sets,aurora::trace::Render);
}
extern "C" void prime_ps4_bind_group_usage_counts(uint64_t hits,uint64_t misses) {
    aurora::trace::count("Dawn bind-group usage cache hits",hits,aurora::trace::Render);
    aurora::trace::count("Dawn bind-group usage cache misses",misses,aurora::trace::Render);
}
extern "C" void prime_ps4_trace_flip(uint64_t ns,int32_t result) {
    using namespace aurora::trace;
    const auto now=now_ns();
    emit({frame_id(Render),now>=ns?now-ns:0,ns,"video-out flip submission CPU (inclusive)",Render,Inclusive});
    count("video-out flip submissions",1,Render);
    if(result<0) count("video-out flip errors",1,Render);
    if(!ns) count("flip duration below microsecond clock resolution",1,Render);
}
