#define _POSIX_C_SOURCE 200809L
#include "../platform/ps4/graphics_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    float scale=1,min=1;bool dynamic=true;int msaa=4,aniso=1,target=30;
    prime_ps4_graphics_defaults(&scale,&dynamic,&msaa,&aniso,&target,&min,false);
    assert(scale==0 && !dynamic && msaa==1 && aniso==16 && target==60 && min==1.5f);
    // A saved revision must preserve manual settings on a later launch.
    scale=1.75f;dynamic=true;msaa=4;aniso=8;target=30;min=1;
    prime_ps4_graphics_defaults(&scale,&dynamic,&msaa,&aniso,&target,&min,false);
    assert(scale==1.75f && dynamic && msaa==4 && aniso==8 && target==30 && min==1);
    // Never overwrite a user's deliberate Original experience.
    prime_ps4_graphics_revision=0;
    prime_ps4_graphics_defaults(&scale,&dynamic,&msaa,&aniso,&target,&min,true);
    assert(prime_ps4_graphics_revision==1 && scale==1.75f && msaa==4 && target==30);
    setenv("MP_PS4_CANDIDATE","original-hd-v0022",1);
    prime_ps4_graphics_revision=1;
    prime_ps4_graphics_defaults(&scale,&dynamic,&msaa,&aniso,&target,&min,false);
    assert(scale==0 && !dynamic && msaa==4 && aniso==16 && target==60 && prime_ps4_graphics_revision==2);
    msaa=1;aniso=4;scale=1.5f;
    prime_ps4_graphics_defaults(&scale,&dynamic,&msaa,&aniso,&target,&min,false);
    assert(msaa==1 && aniso==4 && scale==1.5f);
    setenv("MP_PS4_CANDIDATE","consolidated-original-hd",1);
    assert(prime_ps4_original_hd_default_pending());
    prime_ps4_graphics_defaults(&scale,&dynamic,&msaa,&aniso,&target,&min,false);
    assert(prime_ps4_graphics_revision==3 && !prime_ps4_original_hd_default_pending());
    assert(scale==0 && !dynamic && msaa==4 && aniso==16 && target==60);
    // Remastered can be reenabled by the player; the migration never repeats.
    msaa=1;scale=1.75f;aniso=8;
    prime_ps4_graphics_defaults(&scale,&dynamic,&msaa,&aniso,&target,&min,false);
    assert(msaa==1 && scale==1.75f && aniso==8);
    puts("Native picture migration, retained user choices and Original experience passed");
}
