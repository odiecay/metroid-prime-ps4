"""Exercise production watchdog policy and the driver's real fence-query body."""
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/submit-watchdog-tests'


class SubmitWatchdogTests(unittest.TestCase):
    def test_elapsed_progress_policy_sanitized(self):
        OUT.mkdir(exist_ok=True)
        program=OUT/'policy'
        subprocess.run(['gcc-14','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        '-I'+str(ROOT/'renderer/orbis'),str(ROOT/'tests/submit_watchdog.c'),
                        '-o',str(program)],check=True)
        for _ in range(10):
            subprocess.run([program],check=True,stdout=subprocess.PIPE)

    def test_actual_fence_query_preserves_timeouts_and_completion(self):
        source=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        body=source.split('int\nac_drm_cs_query_fence_status(',1)[1].split('\n/* ---------------------------------------------------------------- refusals',1)[0]
        prelude=r'''
#include "submit_watchdog.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
typedef void ac_drm_device;
static uint32_t label_word;
static volatile uint32_t *orbis_fence_label=&label_word;
static uint64_t fake_now;
static unsigned warnings,polls,complete_after;
static bool failed_clock;
static bool orbis_trace(void) {return false;}
#define mesa_logi(...) ((void)0)
#define mesa_logw(...) (++warnings)
static uint64_t os_time_get_nano(void) {return failed_clock?UINT64_MAX:fake_now;}
static uint64_t os_time_get_absolute_timeout(uint64_t duration) {
 return duration>UINT64_MAX-fake_now?UINT64_MAX:fake_now+duration;
}
struct orbis_wait_watch {uint64_t deadline;};
static void orbis_wait_begin(struct orbis_wait_watch *w,uint64_t deadline,const char *what) {
 (void)what;w->deadline=deadline;
}
static bool orbis_wait_continue(struct orbis_wait_watch *w) {return fake_now<w->deadline;}
static void orbis_poll_pause(void) {
 fake_now+=50000000;++polls;
 if(polls==complete_after) label_word=1;
}
int ac_drm_cs_query_fence_status('''
        main=r'''
int main(void) {
 uint32_t expired=9;
 assert(ac_drm_cs_query_fence_status(NULL,0,0,0,0,1,0,0,&expired)==0 && expired==0 && polls==0);
 assert(ac_drm_cs_query_fence_status(NULL,0,0,0,0,1,100000000,0,&expired)==0 && expired==0);
 assert(polls==2 && warnings==1); // only the existing finite-timeout diagnostic
 fake_now=0;polls=warnings=0;complete_after=8;
 assert(ac_drm_cs_query_fence_status(NULL,0,0,0,0,1,500000000,0,&expired)==0 && expired==1);
 assert(fake_now==400000000 && warnings==1); // progress warning never returns success early
 fake_now=0;polls=warnings=0;complete_after=8;label_word=0;
 assert(ac_drm_cs_query_fence_status(NULL,0,0,0,0,1,UINT64_MAX,1,&expired)==0 && expired==1);
 assert(fake_now==400000000 && warnings==1); // infinite caller deadline remains infinite
 fake_now=0;polls=warnings=0;complete_after=0;label_word=0;failed_clock=true;
 assert(ac_drm_cs_query_fence_status(NULL,0,0,0,0,1,500000000,0,&expired)==0 && expired==0);
 assert(fake_now==500000000 && warnings==1); // bad diagnostic clock cannot fabricate a stall
 puts("actual fence query: zero/finite/infinite waits and failed diagnostic clock preserve completion and deadlines");
 return 0;
}
'''
        OUT.mkdir(exist_ok=True)
        unit=OUT/'actual_fence_query.c';unit.write_text(prelude+body+main)
        program=OUT/'actual_fence_query'
        subprocess.run(['gcc-14','-std=c11','-O1','-g','-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer','-I'+str(ROOT/'renderer/orbis'),str(unit),'-o',str(program)],check=True)
        for _ in range(10):
            subprocess.run([program],check=True,stdout=subprocess.PIPE)


if __name__=='__main__':unittest.main()
