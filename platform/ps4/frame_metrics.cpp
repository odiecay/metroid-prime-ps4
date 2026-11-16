// SPDX-License-Identifier: MIT
#include "frame_metrics.hpp"
#include <cstdio>
extern "C" void prime_ps4_frame_heartbeat(unsigned frame,int64_t now) {
    static prime::FrameMetrics metrics;
    static int64_t previous{},start{};
    if(previous && now>previous) metrics.add(uint64_t(now-previous));
    previous=now;
    if(!start) start=now;
    if(now-start<5000000000ll) return;
    const auto s=metrics.summary();
    std::fprintf(stdout,"orbis: frame times: frame=%u fps=%.2f p50=%.2f ms p95=%.2f ms "
        "p99=%.2f ms worst=%.2f ms frames=%llu samples=%llu unsampled=%llu\n",
        frame,s.fps(),s.p50Ns/1e6,s.p95Ns/1e6,s.p99Ns/1e6,s.worstNs/1e6,
        (unsigned long long)s.frames,(unsigned long long)s.samples,(unsigned long long)s.unsampled);
    metrics.reset();start=now;
}
