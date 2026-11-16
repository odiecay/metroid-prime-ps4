#include "../platform/ps4/frame_metrics.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    prime::FrameMetrics m;
    auto s=m.summary();assert(s.frames==0 && s.fps()==0);
    for(unsigned n=0;n<299;++n) m.add(16666667);
    m.add(500000000); // Keep a genuine loading stall in the distribution.
    s=m.summary();assert(s.frames==300 && s.samples==300 && s.unsampled==0);
    assert(s.p50Ns==16666667 && s.p95Ns==16666667 && s.p99Ns==16666667);
    assert(s.worstNs==500000000 && s.fps()<55);
    m.reset();m.add(0);assert(m.summary().frames==0);
    for(unsigned n=1;n<=100;++n) m.add(n*1000000ull);
    s=m.summary();assert(s.p50Ns==50000000 && s.p95Ns==95000000 && s.p99Ns==99000000);
    m.reset();
    for(unsigned n=0;n<1200;++n) m.add(10000000);
    s=m.summary();assert(s.frames==1200 && s.samples==1024 && s.unsampled==176);
    assert(std::abs(s.fps()-100)<0.001);
    puts("Frame percentiles retain stalls, reset correctly and expose sample overflow");
}
