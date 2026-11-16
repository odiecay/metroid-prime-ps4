// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static int32_t answer;static uint64_t clock_value=100;static unsigned traced,calls;
uint64_t sceKernelGetProcessTime(void) {clock_value+=3;return clock_value;}
int32_t __real_sceVideoOutSubmitFlip(int32_t h,int32_t i,uint32_t m,int64_t a) {
    assert(h==7 && i==2 && m==1 && a==0x123456789abcdefLL);++calls;return answer;
}
void prime_ps4_trace_flip(uint64_t ns,int32_t result) {assert(ns==3000 && result==answer);++traced;}
extern int32_t __wrap_sceVideoOutSubmitFlip(int32_t,int32_t,uint32_t,int64_t);
int main(void) {
    for(unsigned i=0;i<3;++i) {
        answer=i==0?0:i==1?-1:-2147418111;
        assert(__wrap_sceVideoOutSubmitFlip(7,2,1,0x123456789abcdefLL)==answer);
    }
    assert(calls==3 && traced==3);puts("Real flip arguments, success/error passthrough and CPU timing verified; no completion inferred");
}
