// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <orbis/libkernel.h>
// Observe the real CPU API call; a successful return is not display completion.
extern int32_t __real_sceVideoOutSubmitFlip(int32_t,int32_t,uint32_t,int64_t);
extern void prime_ps4_trace_flip(uint64_t,int32_t);
int32_t __wrap_sceVideoOutSubmitFlip(int32_t handle,int32_t index,uint32_t mode,int64_t argument) {
    const uint64_t before=sceKernelGetProcessTime();
    const int32_t result=__real_sceVideoOutSubmitFlip(handle,index,mode,argument);
    const uint64_t after=sceKernelGetProcessTime();
    prime_ps4_trace_flip(after>=before?(after-before)*1000:0,result);
    return result;
}
