// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace prime {
enum class ReclaimStatus { BelowThreshold, WaitFailed, NewWorkPending, Reclaimed };
constexpr uint64_t RetiredPressureBytes=128ull*1024*1024;
constexpr uint64_t FreeReserveBytes=512ull*1024*1024;
constexpr uint64_t ContiguousReserveBytes=128ull*1024*1024;
inline bool should_wait_for_retirement(uint64_t retired,uint64_t free,uint64_t largest) {
    return retired>=RetiredPressureBytes &&
        (free<FreeReserveBytes || largest<ContiguousReserveBytes);
}
template<class Wait,class Drain>
ReclaimStatus reclaim_under_pressure(uint64_t retired,Wait wait,Drain drain) {
    if(retired<RetiredPressureBytes) return ReclaimStatus::BelowThreshold;
    if(!wait(500000000ull)) return ReclaimStatus::WaitFailed;
    // The driver rechecks its actual latest submission under its submit lock.
    if(!drain()) return ReclaimStatus::NewWorkPending;
    return ReclaimStatus::Reclaimed;
}
}
