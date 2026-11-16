// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int prime_ps4_gameplay_revision;
void prime_ps4_gameplay_defaults(bool *remasteredMovement,bool *quickResume);
bool prime_ps4_snapshot_position_in_bounds(const float position[3],const float bounds[6]);
#ifdef __cplusplus
}
#endif
