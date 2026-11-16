// SPDX-License-Identifier: MIT
#include "graphics_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int prime_ps4_graphics_revision;
bool prime_ps4_original_hd_default_pending(void) {
    const char* profile=getenv("MP_PS4_CANDIDATE");
    return profile && !strcmp(profile,"consolidated-original-hd") && prime_ps4_graphics_revision<3;
}
void prime_ps4_graphics_defaults(float *scale,bool *dynamic,int *msaa,int *anisotropy,
                                 int *target,float *minimum,bool original) {
    const char* profile=getenv("MP_PS4_CANDIDATE");
    if(prime_ps4_original_hd_default_pending()) {
        prime_ps4_graphics_revision=3;
        *scale=0.f;*dynamic=false;*msaa=4;*anisotropy=16;*target=60;*minimum=1.5f;
        puts("orbis: Original HD defaults v3: native output, 4x MSAA, 16x anisotropy; later visual choices preserved");
        return;
    }
    if(!original && profile && !strcmp(profile,"original-hd-v0022") && prime_ps4_graphics_revision<2) {
        prime_ps4_graphics_revision=2;
        *scale=0.f;*dynamic=false;*msaa=4;*anisotropy=16;*target=60;*minimum=1.5f;
        puts("orbis: Original HD profile: native output, 4x MSAA, 16x anisotropy; original effects retained");
        return;
    }
    if(prime_ps4_graphics_revision>=1) return;
    prime_ps4_graphics_revision=1;
    if(original) {
        puts("orbis: picture defaults preserve the selected Original experience");
        return;
    }
    // Auto follows the real 1920x1080 surface and the active NTSC/PAL viewport.
    // Keep a fixed native picture for the first measured quality comparison.
    // Add MSAA only after real GPU-time and memory measurements establish room.
    *scale=0.f;*dynamic=false;*msaa=1;*anisotropy=16;*target=60;*minimum=1.5f;
    puts("orbis: picture defaults v1: native surface resolution, 16x anisotropy cap, "
         "MSAA 1x, dynamic resolution off; later user settings are preserved");
}
