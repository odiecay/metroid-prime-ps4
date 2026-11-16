// SPDX-License-Identifier: MIT
// Fault-inject the actual native display-retirement guard; no GPU is simulated.
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <orbis/VideoOut.h>

extern int32_t __wrap_sceVideoOutClose(int32_t);
static unsigned mode,queries,closes;
static uint64_t elapsed;
static jmp_buf escaped;
uint64_t sceKernelGetProcessTime(void) { return elapsed; }
int32_t sceKernelUsleep(uint32_t us) { elapsed+=us;return 0; }
int32_t sceVideoOutGetFlipStatus(int32_t handle,OrbisVideoOutFlipStatus *out) {
    assert(handle==3);++queries;
    if(mode==2) return -1;
    out->numFlipPending=mode==3?1:mode==1 && queries<3?1:0;
    return 0;
}
int32_t __real_sceVideoOutClose(int32_t handle) { assert(handle==3);++closes;return mode==4?-1:0; }
__attribute__((noreturn)) void prime_test_exit(int status) { assert(status==7);longjmp(escaped,1); }
int main(void) {
    for(mode=0;mode<5;++mode) {
        queries=closes=0;elapsed=0;
        if(setjmp(escaped)==0) {
            assert(__wrap_sceVideoOutClose(3)==0);
            assert(mode<2 && closes==1);
            if(mode==1) assert(queries==3 && elapsed==1000);
        } else {
            assert(mode>=2);
            assert(closes==(mode==4?1u:0u));
            if(mode==3) assert(elapsed==2000000);
        }
    }
    puts("native display retirement: success, drain, query error, timeout and close error passed");
}
