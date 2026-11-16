"""Failure-injection checks for the actual pressure policy and locked driver gate."""
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class GpuRetirementTests(unittest.TestCase):
    def test_retirement_finishes_old_backing_protection_before_va_reuse(self):
        source=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        body=source.split('static void\norbis_retire_ranges(void)\n{',1)[1].split('\n}\n',1)[0]
        unit=ROOT/'build/retirement-backing-order.c'
        unit.write_text('''#define __PS4__ 1
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
static bool orbis_arena_unmapped=true;
static uint32_t label=1,*orbis_fence_label=&label;
static uint64_t orbis_submit_seq_no=1;
struct amdgpu_va {uint64_t base,size;bool retired;struct amdgpu_va *next;};
static struct amdgpu_va *orbis_va_list;
static int orbis_va_lock,orbis_retire_lock,protects,restores;
#define ORBIS_RETIRE_MAX 8
static struct orbis_retire {
 uint64_t addr,size,seq,garlic_addr,garlic_bytes;int64_t garlic_phys;
} orbis_retire_q[ORBIS_RETIRE_MAX];
static unsigned orbis_retire_head,orbis_retire_count;
static void simple_mtx_lock(int *p){assert(!*p);*p=1;}
static void simple_mtx_unlock(int *p){assert(*p);*p=0;}
static void orbis_garlic_put_back(uint64_t a,uint64_t n,int64_t p) {
 assert(orbis_va_lock && orbis_retire_lock);++restores;
}
static void orbis_protect_now(uint64_t a,uint64_t n) {
 // Allocation cannot reuse an unlinked address while its old protection lands.
 assert(orbis_va_lock && orbis_retire_lock && restores>protects);++protects;
}
static void orbis_retire_ranges(void) {'''+body+'''
}
int main(void) {
 struct amdgpu_va *old=calloc(1,sizeof(*old)),*live=calloc(1,sizeof(*live));
 old->retired=true;old->next=live;orbis_va_list=old;
 orbis_retire_count=1;orbis_retire_q[0]=(struct orbis_retire){.addr=0x10000,.size=16384,.seq=1};
 label=0;orbis_retire_ranges();
 assert(orbis_va_list==old && orbis_retire_count==1 && protects==0);
 label=1;orbis_retire_ranges();
 assert(orbis_va_list==live && orbis_retire_count==0 && protects==1 && restores==1);
 assert(!orbis_va_lock && !orbis_retire_lock);free(live);
}
''')
        program=ROOT/'build/retirement-backing-order'
        subprocess.run(['gcc-14','-std=c11','-O2','-fsanitize=address,undefined',str(unit),'-o',str(program)],check=True)
        subprocess.run([program],check=True,timeout=10)
    def test_allocation_pressure_wait_keeps_ranges_on_timeout_or_incomplete_fence(self):
        source=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        body=source.split('bool\nprime_orbis_reclaim_for_allocation(uint64_t timeout_ns)\n{',1)[1].split('\n}\n',1)[0]
        unit=ROOT/'build/retirement-allocation-gate.c'
        unit.write_text('''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
static int orbis_submit_lock,drains,waits;
static uint32_t label;
static uint32_t *orbis_fence_label=&label;
static uint64_t orbis_submit_seq_no;
static bool wait_result;
static void simple_mtx_lock(int *p) {assert(!*p);*p=1;}
static void simple_mtx_unlock(int *p) {assert(*p);*p=0;}
static bool ac_orbis_wait_gpu_idle(uint64_t t) {
 assert(orbis_submit_lock && t==500000000ull);++waits;return wait_result;
}
static void orbis_retire_ranges(void) {assert(orbis_submit_lock);++drains;}
bool prime_orbis_reclaim_for_allocation(uint64_t timeout_ns) {'''+body+'''
}
int main(void) {
 orbis_submit_seq_no=2;label=1;wait_result=false;
 assert(!prime_orbis_reclaim_for_allocation(500000000ull) && waits==1 && drains==0 && !orbis_submit_lock);
 wait_result=true;
 assert(!prime_orbis_reclaim_for_allocation(500000000ull) && waits==2 && drains==0);
 label=2;assert(prime_orbis_reclaim_for_allocation(500000000ull) && drains==1);
 orbis_submit_seq_no=0x100000000ull;label=UINT32_MAX;
 assert(!prime_orbis_reclaim_for_allocation(500000000ull) && drains==1);
 label=0;assert(prime_orbis_reclaim_for_allocation(500000000ull) && drains==2);
 orbis_fence_label=0;
 assert(!prime_orbis_reclaim_for_allocation(500000000ull) && drains==2 && waits==5);
}
''')
        program=ROOT/'build/retirement-allocation-gate'
        subprocess.run(['gcc-14','-std=c11','-O2','-fsanitize=address,undefined',str(unit),'-o',str(program)],check=True)
        subprocess.run([program],check=True,timeout=10)
    def test_pressure_wait_and_failure_paths(self):
        program=ROOT/'build/retirement-policy-host'
        subprocess.run(['clang++-19','-std=c++20','-O2',str(ROOT/'tests/retirement_policy.cpp'),'-o',str(program)],check=True)
        self.assertIn('retains ranges on timeout/new work',subprocess.check_output([program],text=True))
    def test_driver_rechecks_latest_fence_under_submit_lock(self):
        source=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        body=source.split('bool\nprime_orbis_reclaim_completed(void)\n{',1)[1].split('\n}\n',1)[0]
        unit=ROOT/'build/retirement-driver-gate.c'
        unit.write_text('''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
static int orbis_submit_lock,drains;
static uint32_t label;
static uint32_t *orbis_fence_label=&label;
static uint64_t orbis_submit_seq_no;
static void simple_mtx_lock(int *p) {assert(!*p);*p=1;}
static void simple_mtx_unlock(int *p) {assert(*p);*p=0;}
static void orbis_retire_ranges(void) {assert(orbis_submit_lock);++drains;}
bool prime_orbis_reclaim_completed(void) {'''+body+'''
}
int main(void) {
 orbis_submit_seq_no=1;label=0;
 assert(!prime_orbis_reclaim_completed() && drains==0 && !orbis_submit_lock);
 label=1;assert(prime_orbis_reclaim_completed() && drains==1 && !orbis_submit_lock);
 orbis_submit_seq_no=2;assert(!prime_orbis_reclaim_completed() && drains==1);
 orbis_submit_seq_no=0x100000000ull;label=UINT32_MAX;
 assert(!prime_orbis_reclaim_completed() && drains==1);
 label=0;assert(prime_orbis_reclaim_completed() && drains==2);
 orbis_fence_label=0;assert(!prime_orbis_reclaim_completed() && drains==2);
}
''')
        program=ROOT/'build/retirement-driver-gate'
        subprocess.run(['gcc-14','-std=c11','-O2','-fsanitize=address,undefined',str(unit),'-o',str(program)],check=True)
        subprocess.run([program],check=True,timeout=10)
if __name__=='__main__':unittest.main()
