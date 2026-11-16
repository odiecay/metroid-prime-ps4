// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct PrimeOrbisVaStats {
    uint64_t capacity,live,retired,free,largest_free,ranges,retired_ranges;
};
struct PrimeOrbisVaWalk {
    struct PrimeOrbisVaStats stats;
    uint64_t cursor,end;
};
static inline bool prime_va_begin(struct PrimeOrbisVaWalk *walk,uint64_t base,uint64_t end) {
#ifdef __cplusplus
    *walk={};
#else
    *walk=(struct PrimeOrbisVaWalk){0};
#endif
    if(end<base) return false;
    walk->cursor=base;walk->end=end;walk->stats.capacity=end-base;
    return true;
}
static inline bool prime_va_add(struct PrimeOrbisVaWalk *walk,uint64_t base,uint64_t bytes,bool retired) {
    if(base<walk->cursor || base>walk->end || bytes>walk->end-base) return false;
    const uint64_t gap=base-walk->cursor;
    if(gap>walk->stats.largest_free) walk->stats.largest_free=gap;
    walk->cursor=base+bytes;
    ++walk->stats.ranges;
    if(retired) { walk->stats.retired+=bytes;++walk->stats.retired_ranges; }
    else walk->stats.live+=bytes;
    return true;
}
static inline void prime_va_finish(struct PrimeOrbisVaWalk *walk) {
    const uint64_t tail=walk->end-walk->cursor;
    if(tail>walk->stats.largest_free) walk->stats.largest_free=tail;
    walk->stats.free=walk->stats.capacity-walk->stats.live-walk->stats.retired;
}
#ifdef __cplusplus
extern "C" {
#endif
// Snapshot under the actual driver's allocation lock; no ownership is changed.
bool prime_orbis_va_snapshot(struct PrimeOrbisVaStats *out);
bool prime_orbis_reclaim_completed(void);
// Allocation-failure fallback: hold the submission lock, wait for the actual
// latest native fence, and retire only after completion. No Dawn reentry.
bool prime_orbis_reclaim_for_allocation(uint64_t timeout_ns);
#ifdef __cplusplus
}
#endif
