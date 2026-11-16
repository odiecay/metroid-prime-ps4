// SPDX-License-Identifier: MIT
// Do not return to WSI's image destruction if display retirement is unconfirmed.
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "lifecycle.h"

extern int32_t __real_sceVideoOutClose(int32_t handle);
int32_t __wrap_sceVideoOutClose(int32_t handle) {
    OrbisVideoOutFlipStatus status={0};
    const uint64_t deadline=sceKernelGetProcessTime()+2000000;
    int32_t queried;
    do {
        queried=sceVideoOutGetFlipStatus(handle,&status);
        if(queried<0 || status.numFlipPending==0) break;
        sceKernelUsleep(500);
    } while(sceKernelGetProcessTime()<deadline);
    if(queried<0 || status.numFlipPending!=0) {
        fprintf(stderr,"orbis: display retirement unconfirmed: handle=%d status=0x%08x pending=%d; retaining resources until process exit\n",
                handle,(unsigned)queried,(int)status.numFlipPending);
        fflush(stderr);
        prime_ps4_exit_to_shell(7);
    }
    const int32_t result=__real_sceVideoOutClose(handle);
    fprintf(stderr,"orbis: display close: handle=%d pending=%d result=0x%08x\n",
            handle,(int)status.numFlipPending,(unsigned)result);
    if(result<0) {
        fflush(stderr);
        prime_ps4_exit_to_shell(7);
    }
    return result;
}
