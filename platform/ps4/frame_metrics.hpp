// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace prime {
struct FrameSummary {
    uint64_t frames{},samples{},unsampled{},sumNs{},worstNs{},p50Ns{},p95Ns{},p99Ns{};
    uint64_t over1667{},over20{},over25{},over3333{};
    double fps() const { return sumNs?double(frames)*1e9/double(sumNs):0; }
};
class FrameMetrics {
    // 17 seconds at the 60 FPS target. No allocation on the frame path.
    std::array<uint64_t,1024> times_{};
    uint64_t count_{},sum_{},worst_{};
    std::array<uint64_t,4> over_{};
public:
    void add(uint64_t ns) {
        if(!ns) return;
        if(count_<times_.size()) times_[count_]=ns;
        ++count_;sum_+=ns;worst_=std::max(worst_,ns);
        over_[0]+=ns>16666667;over_[1]+=ns>20000000;over_[2]+=ns>25000000;over_[3]+=ns>33333333;
    }
    FrameSummary summary() const {
        if (!count_) return {};
        const size_t n=std::min<uint64_t>(count_,times_.size());
        std::array<uint64_t,1024> sorted;
        std::copy_n(times_.begin(),n,sorted.begin());
        std::sort(sorted.begin(),sorted.begin()+n);
        const auto percentile=[&](size_t p) { return n?sorted[(n*p+99)/100-1]:0; };
        return {count_,n,count_-n,sum_,worst_,percentile(50),percentile(95),percentile(99),over_[0],over_[1],over_[2],over_[3]};
    }
    void reset() { count_=sum_=worst_=0;over_={}; }
};
}
