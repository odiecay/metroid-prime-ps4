// SPDX-License-Identifier: MIT
#include "gameplay_policy.h"
#include <math.h>
#include <stdio.h>
int prime_ps4_gameplay_revision;
void prime_ps4_gameplay_defaults(bool *remasteredMovement,bool *quickResume) {
    if(prime_ps4_gameplay_revision>=1) return;
    prime_ps4_gameplay_revision=1;
    *remasteredMovement=false;
    *quickResume=false;
    puts("orbis: gameplay defaults v1: original movement/collision, automatic quick resume off; "
         "existing aim, picture settings and card files preserved");
}
bool prime_ps4_snapshot_position_in_bounds(const float p[3],const float b[6]) {
    // Four world units allow the player's bounds at door/room edges. Falling
    // far outside every current/loaded area's bounds must not replace a snapshot.
    for(unsigned i=0;i<3;++i) {
        if(!isfinite(p[i]) || !isfinite(b[i]) || !isfinite(b[i+3]) || b[i]>b[i+3] ||
           p[i]<b[i]-4.f || p[i]>b[i+3]+4.f) return false;
    }
    return true;
}
