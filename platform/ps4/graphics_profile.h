// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int prime_ps4_graphics_revision;
bool prime_ps4_original_hd_default_pending(void);
void prime_ps4_graphics_defaults(float *scale,bool *dynamic,int *msaa,int *anisotropy,
                                 int *target,float *minimum,bool original);
#ifdef __cplusplus
}
#endif
