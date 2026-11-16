#include "../platform/ps4/retirement_policy.hpp"
#include <cassert>
#include <cstdio>
int main() {
    using prime::ReclaimStatus;int waits=0,drains=0;bool complete=false,lockedComplete=false;
    assert(!prime::should_wait_for_retirement(1,0,0)); // no retired resources to recover
    assert(!prime::should_wait_for_retirement(prime::RetiredPressureBytes,1024ull<<20,256ull<<20));
    assert(prime::should_wait_for_retirement(prime::RetiredPressureBytes,1024ull<<20,64ull<<20));
    assert(prime::should_wait_for_retirement(prime::RetiredPressureBytes,256ull<<20,256ull<<20));
    auto wait=[&](uint64_t timeout) {assert(timeout==500000000ull);++waits;return complete;};
    auto drain=[&] {assert(complete && waits>0);++drains;return lockedComplete;};
    assert(prime::reclaim_under_pressure(1,wait,drain)==ReclaimStatus::BelowThreshold && waits==0 && drains==0);
    assert(prime::reclaim_under_pressure(prime::RetiredPressureBytes,wait,drain)==ReclaimStatus::WaitFailed && drains==0);
    complete=true;
    assert(prime::reclaim_under_pressure(prime::RetiredPressureBytes,wait,drain)==ReclaimStatus::NewWorkPending && drains==1);
    lockedComplete=true;
    assert(prime::reclaim_under_pressure(prime::RetiredPressureBytes,wait,drain)==ReclaimStatus::Reclaimed && drains==2);
    puts("Retirement waits before draining, retains ranges on timeout/new work and respects pressure threshold");
}
