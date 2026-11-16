#include "../platform/ps4/gameplay_policy.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    bool alternate=true,resume=true;
    prime_ps4_gameplay_defaults(&alternate,&resume);
    assert(!alternate && !resume && prime_ps4_gameplay_revision==1);
    alternate=true;resume=true;
    prime_ps4_gameplay_defaults(&alternate,&resume);
    assert(alternate && resume); // preserve a later explicit user choice
    float box[6]={-10,-10,-20,10,10,20},p[3]={0,0,0};
    assert(prime_ps4_snapshot_position_in_bounds(p,box));
    p[2]=-24;assert(prime_ps4_snapshot_position_in_bounds(p,box));
    p[2]=-24.1f;assert(!prime_ps4_snapshot_position_in_bounds(p,box));
    p[2]=-33000;assert(!prime_ps4_snapshot_position_in_bounds(p,box));
    p[2]=NAN;assert(!prime_ps4_snapshot_position_in_bounds(p,box));
    p[2]=INFINITY;assert(!prime_ps4_snapshot_position_in_bounds(p,box));
    p[2]=0;box[5]=-30;assert(!prime_ps4_snapshot_position_in_bounds(p,box));
    puts("Gameplay migration preserves later choices; snapshots reject void/nonfinite positions");
}
