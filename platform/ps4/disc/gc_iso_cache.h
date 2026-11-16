// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Process-wide monotonic counters; retained bytes is current owned cache data.
// Counter deltas describe I/O work, not CPU or GPU execution time.
typedef struct PrimeIsoReadCacheMetrics {
    uint64_t block_hits, block_misses, source_read_calls, source_read_bytes;
    uint64_t retained_bytes, exact_fallbacks, bulk_bypass_requests;
} PrimeIsoReadCacheMetrics;
void prime_iso_read_cache_metrics(PrimeIsoReadCacheMetrics *out);
#ifdef __cplusplus
}
#endif
