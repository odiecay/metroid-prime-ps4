// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PrimeTimestampCaps {
    uint32_t queue_family,valid_bits,compute_and_graphics;
    float period_ns;
} PrimeTimestampCaps;
// Selected native Vulkan queue only. No submission, polling or GPU wait.
bool prime_ps4_timestamp_caps(void* device,PrimeTimestampCaps* caps);
#ifdef __cplusplus
}
#endif
