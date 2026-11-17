#include <aurora/frame_trace.hpp>
// SPDX-License-Identifier: MIT
#include "sdl/orbis_pad.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dawn/native/DawnNative.h>
#include <aurora/gfx.h>
#include <orbis/libkernel.h>
#include "../../renderer/orbis/va_statistics.h"
#include "retirement_policy.hpp"
#include "../../renderer/orbis/timestamp_capabilities.h"
#include "lifecycle.h"
extern "C" bool prime_ps4_wait_for_gpu(WGPUDevice,uint64_t);
#include <orbis/SystemService.h>
extern "C" void prime_ps4_report_fatal(const char *,const char *,unsigned);
extern "C" int wsi_orbis_prepare_present(uint32_t*,uint32_t*,uint32_t*,uint32_t*);
namespace aurora {
void prime_orbis_prepare_present() {
    static const bool enabled=[] {
        const char* value=std::getenv("MP_PS4_PRESENT_PREWAIT");
        return !value || std::strcmp(value,"0")!=0;
    }();
    if (!enabled) return;
    trace::Scope wait("video-out slot prewait (inclusive)",trace::Render);
    uint32_t before=0,capacity=0,after=0,polls=0;
    const int status=wsi_orbis_prepare_present(&before,&capacity,&after,&polls);
    trace::count("present prewait queue depth",before,trace::Render);
    trace::count("present prewait queue capacity",capacity,trace::Render);
    trace::count("present prewait release depth",after,trace::Render);
    trace::count("present prewait status polls",polls,trace::Render);
    trace::count(status==1 ? "present prewait ready" : status==0 ? "present prewait unavailable" :
        status==-1 ? "present prewait status error" : "present prewait timeout",1,trace::Render);
}
namespace gfx { void prime_orbis_encode_stats(uint64_t*); }
namespace gfx::detail { void prime_orbis_command_pool_stats(uint64_t*); }
void prime_orbis_gpu_times(float*,float*,uint32_t*,bool*);
// Called on the render worker after ProcessEvents, outside any Dawn API lock.
// These are Dawn resource estimates, not a claim about total RADV/driver use.
void prime_orbis_gpu_maintenance(WGPUDevice device) {
    trace::Scope maintenance("GPU resource retirement (inclusive)", trace::Render);
    static uint64_t nextSample = 0;
    const uint64_t now = sceKernelGetProcessTime();
    if (!device) return;
    PrimeOrbisVaStats va{};
    bool valid=prime_orbis_va_snapshot(&va);
    // Retirement is frame maintenance, not a five-second logging task. Prefer
    // a nonblocking completed-fence drain; wait only when actual free-space or
    // fragmentation pressure needs it. Allocation bursts also have a bounded
    // driver-level fallback, outside Dawn's API/device lock.
    if (valid && va.retired>=prime::RetiredPressureBytes) {
        const uint64_t retired=va.retired,start=sceKernelGetProcessTime();
        const bool reclaimed=prime_orbis_reclaim_completed();
        int status=reclaimed?int(prime::ReclaimStatus::Reclaimed):-1;
        if (!reclaimed && prime::should_wait_for_retirement(va.retired,va.free,va.largest_free)) {
            const auto result=prime::reclaim_under_pressure(retired,
                [device](uint64_t timeout) { trace::Scope wait("retirement fence wait (inclusive)", trace::Render); return prime_ps4_wait_for_gpu(device,timeout); },
                [] { return prime_orbis_reclaim_completed(); });
            status=int(result);
        }
        if (status>=0) {
            valid=prime_orbis_va_snapshot(&va);
            std::fprintf(stdout,"orbis: GPU retirement: status=%d held=%llu->%llu KiB wait=%.3f ms\n",
                status,(unsigned long long)(retired/1024),
                (unsigned long long)(valid?va.retired/1024:0),double(sceKernelGetProcessTime()-start)/1000.);
        }
    }
    if (now < nextSample) return;
    nextSample = now + 5000000;
    static bool capsReported=false;
    PrimeTimestampCaps caps{};
    if(!capsReported && prime_ps4_timestamp_caps(device,&caps)) {
        capsReported=true;
        std::fprintf(stdout,"orbis: native timestamp capabilities: queue_family=%u valid_bits=%u period_ns=%.6f compute_graphics=%u\n",
            caps.queue_family,caps.valid_bits,double(caps.period_ns),caps.compute_and_graphics);
    }
    uint64_t commandPool[4]{};
    gfx::detail::prime_orbis_command_pool_stats(commandPool);
    std::fprintf(stdout,"orbis: CPU command cache: hits=%llu misses=%llu retained=%llu KiB slots=%llu limit_per_slot=8192 KiB\n",
        (unsigned long long)commandPool[0],(unsigned long long)commandPool[1],
        (unsigned long long)(commandPool[2]/1024),(unsigned long long)commandPool[3]);
    uint64_t bindings[5]{};
    gfx::prime_orbis_encode_stats(bindings);
    std::fprintf(stdout,"orbis: GX encoding: draws=%llu uniform binds=%llu reused=%llu index binds=%llu reused=%llu\n",
        (unsigned long long)bindings[0],(unsigned long long)bindings[1],(unsigned long long)bindings[2],
        (unsigned long long)bindings[3],(unsigned long long)bindings[4]);
    float gpuPassMs=0,gpuSpanMs=0;uint32_t gpuFrames=0;bool gpuSupported=false;
    prime_orbis_gpu_times(&gpuPassMs,&gpuSpanMs,&gpuFrames,&gpuSupported);
    std::fprintf(stdout,"orbis: GPU timestamps: supported=%d frames=%u pass_sum=%.3f ms span=%.3f ms\n",
        int(gpuSupported),gpuFrames,double(gpuPassMs),double(gpuSpanMs));
    const auto before = dawn::native::GetAllocatorMemoryInfo(device);
    const auto live = dawn::native::ComputeEstimatedMemoryUsageInfo(device);
    bool trim=false,pending=false;
    if (valid && va.free<512ull*1024*1024) {
        trim=true;
        pending=dawn::native::ReduceMemoryUsage(device);
        valid=prime_orbis_va_snapshot(&va);
    }
    const auto after = dawn::native::GetAllocatorMemoryInfo(device);
    if (valid) {
        std::fprintf(stdout,"orbis: GPU VA: capacity=%llu KiB live=%llu KiB retired=%llu KiB "
            "free=%llu KiB largest=%llu KiB ranges=%llu retired_ranges=%llu\n",
            (unsigned long long)(va.capacity/1024),(unsigned long long)(va.live/1024),
            (unsigned long long)(va.retired/1024),(unsigned long long)(va.free/1024),
            (unsigned long long)(va.largest_free/1024),(unsigned long long)va.ranges,
            (unsigned long long)va.retired_ranges);
    }
    std::fprintf(stdout,
        "orbis: GPU memory: Dawn used=%llu KiB heaps=%llu->%llu KiB "
        "live buffers=%llu KiB textures=%llu KiB pipelines=%u trim_requested=%d trim_pending=%d\n",
        (unsigned long long)(before.totalUsedMemory/1024),
        (unsigned long long)(before.totalAllocatedMemory/1024),
        (unsigned long long)(after.totalAllocatedMemory/1024),
        (unsigned long long)(live.buffersUsage/1024),
        (unsigned long long)(live.texturesUsage/1024),
        aurora_get_pipeline_count(), int(trim), int(pending));
}
void prime_orbis_fatal_message(const char *module,const char *text,unsigned length) {
    prime_ps4_report_fatal(module,text,length);
}
void prime_orbis_present_queued() {
    static std::atomic_flag shown=ATOMIC_FLAG_INIT;
    if (!shown.test_and_set()) sceSystemServiceHideSplashScreen();
}
}
namespace aurora::window {
void prime_orbis_pump_input() { prime_ps4_pump_pad(); }
}
namespace aurora::input {
void prime_orbis_close_input() { prime_ps4_close_pad(); }
}
namespace PortDebug {
void PrimeOrbisRequestQuit() { prime_ps4_request_quit(); }
}
