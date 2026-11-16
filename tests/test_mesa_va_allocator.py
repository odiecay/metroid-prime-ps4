"""Compile the actual driver's first-fit function with a host memory/list seam."""
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class MesaVaAllocatorTests(unittest.TestCase):
    def test_native_page_fragment_avoids_false_aligned_oom_without_relaxing_resource_alignment(self):
        source=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        self.assertIn('d->pte_fragment_size = (uint32_t)orbis_page_size();',source)
        body=source.split('int\nac_drm_va_range_alloc(',1)[1].split('\nint\nac_drm_va_range_free(',1)[0]
        prelude=r'''
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "va_statistics.h"
typedef void ac_drm_device;
enum amdgpu_gpu_va_range { UNUSED_RANGE };
struct amdgpu_va { uint64_t base,size;struct amdgpu_va *next;bool retired;uint64_t retire_seq; };
typedef struct amdgpu_va* amdgpu_va_handle;
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define AMDGPU_VA_RANGE_HIGH 2
#define AMDGPU_VA_RANGE_32_BIT 1
#define mesa_logi(...) ((void)0)
#define mesa_loge(...) ((void)0)
#define prime_orbis_va_snapshot(out) false
static int orbis_va_lock;
static struct amdgpu_va *orbis_va_list;
static uint64_t orbis_va_base=0x220000000ull,orbis_va_end=0x222000000ull;
static void simple_mtx_lock(int *p) {assert(!*p);*p=1;}
static void simple_mtx_unlock(int *p) {assert(*p);*p=0;}
static uint64_t orbis_page_size(void) {return 16384;}
static uint64_t orbis_align_up(uint64_t v,uint64_t a) {return (v+a-1)&~(a-1);}
static bool orbis_budget(unsigned *p,unsigned max) {(void)p;(void)max;return false;}
int ac_drm_va_range_alloc('''
        test=r'''
static void clear_ranges(void) {
 while(orbis_va_list) {struct amdgpu_va *v=orbis_va_list;orbis_va_list=v->next;free(v);}
}
int main(void) {
 unsetenv("ORBIS_VA_NO_REUSE");unsetenv("ORBIS_NO_VA_GAP");
 const uint64_t MiB=1024*1024,request=8404992;
 struct amdgpu_va *a=NULL,*b=NULL,*result=NULL;uint64_t at=0;
 // Match the crash's 10208 KiB largest free span: the preceding BO's
 // own 16 KiB pad plus our 16 KiB guard offset the free span's start.
 assert(!ac_drm_va_range_alloc(NULL,UNUSED_RANGE,8*MiB+16384,16384,
                               orbis_va_base,&at,&a,0));
 assert(a->size==8*MiB+32768);
 const uint64_t following=orbis_va_base+18*MiB;
 assert(!ac_drm_va_range_alloc(NULL,UNUSED_RANGE,orbis_va_end-following,
                               16384,following,&at,&b,0));
 assert(b->base-(a->base+a->size)==10208*1024);
 // A raw hole larger than the request still fails after the old PTE
 // optimization forces a 2 MiB-aligned start and both guards must fit.
 assert(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,request,2*MiB,0,&at,&result,0)==-ENOMEM);
 assert(result==NULL && a->next==b);
 // The actual caller's 64 KiB resource alignment is retained. Correct
 // native PTE granularity leaves that requirement unchanged and fits.
 const uint64_t native_fragment=orbis_page_size(),resource_alignment=65536;
 const uint64_t actual_alignment=MAX2(native_fragment,resource_alignment);
 assert(!ac_drm_va_range_alloc(NULL,UNUSED_RANGE,request,actual_alignment,0,&at,&result,0));
 assert(at%resource_alignment==0 && result->size==request+16384);
 assert(at>=a->base+a->size && result->base+result->size<=b->base);
 clear_ranges();
 // There is no 255/256 reservation-node cap. More than 1024 live nodes
 // remain aligned, disjoint and guarded, with exact address replay.
 orbis_va_end=orbis_va_base+512*MiB;
 for(unsigned i=0;i<1024;++i) {
  assert(!ac_drm_va_range_alloc(NULL,UNUSED_RANGE,65536,65536,0,&at,&result,0));
  assert(at%65536==0 && result->size==65536+16384);
 }
 struct PrimeOrbisVaWalk walk;assert(prime_va_begin(&walk,orbis_va_base,orbis_va_end));
 for(struct amdgpu_va *v=orbis_va_list;v;v=v->next)assert(prime_va_add(&walk,v->base,v->size,v->retired));
 prime_va_finish(&walk);assert(walk.stats.ranges==1024);
 // A larger explicit alignment still receives its full guarantee.
 assert(!ac_drm_va_range_alloc(NULL,UNUSED_RANGE,request,4*MiB,0,&at,&result,0));
 assert(at%(4*MiB)==0 && result->size==request+16384);
 assert(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,65536,65536,at,&at,&result,0)==-EINVAL);
 clear_ranges();
 puts("8404992-byte allocation reproduces false OOM in 10208-KiB hole at 2-MiB alignment; native page preserves 64-KiB and 4-MiB requirements; 1024 nodes pass");
}
'''
        out=ROOT/'build/mesa-va-alignment';out.mkdir(exist_ok=True)
        unit=out/'allocator.c';unit.write_text(prelude+body+test)
        program=out/'allocator'
        subprocess.run(['gcc-14','-std=c11','-D_GNU_SOURCE','-O2','-fsanitize=address,undefined',
                        '-I'+str(ROOT/'renderer/orbis'),str(unit),'-o',str(program)],check=True)
        result=subprocess.run([program],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertIn('1024 nodes pass',result.stdout)
        print(result.stdout.strip())

    def test_reused_hole_preserves_guard_gap(self):
        source=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        body=source.split('int\nac_drm_va_range_alloc(',1)[1].split('\nint\nac_drm_va_range_free(',1)[0]
        prelude=r'''
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "va_statistics.h"
typedef void ac_drm_device;
enum amdgpu_gpu_va_range { UNUSED_RANGE };
struct amdgpu_va { uint64_t base,size;struct amdgpu_va *next;bool retired;uint64_t retire_seq; };
typedef struct amdgpu_va* amdgpu_va_handle;
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define AMDGPU_VA_RANGE_HIGH 2
#define AMDGPU_VA_RANGE_32_BIT 1
#define mesa_logi(...) ((void)0)
#define mesa_loge(...) ((void)0)
#define prime_orbis_va_snapshot(out) false
static int orbis_va_lock;
static struct amdgpu_va *orbis_va_list;
static uint64_t orbis_va_base=0x220000000ull,orbis_va_end=0x220200000ull;
static void simple_mtx_lock(int *p) {(void)p;}
static void simple_mtx_unlock(int *p) {(void)p;}
static uint64_t orbis_page_size(void) {return 16384;}
static uint64_t orbis_align_up(uint64_t v,uint64_t a) {return (v+a-1)&~(a-1);}
static bool orbis_budget(unsigned *p,unsigned max) {(void)p;(void)max;return false;}
int ac_drm_va_range_alloc('''
        test=r'''
int main(void) {
    unsetenv("ORBIS_VA_NO_REUSE");unsetenv("ORBIS_NO_VA_GAP");
    struct amdgpu_va *a=NULL,*b=NULL,*replacement=NULL;
    uint64_t at=0;
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,16384,16384,0,&at,&a,0)) return 1;
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,16384,16384,0,&at,&b,0)) return 2;
    // Simulate a fully retired A: its 32 KiB reservation becomes a free hole.
    orbis_va_list=b;free(a);
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,32768,16384,0,&at,&replacement,0)) return 3;
    struct PrimeOrbisVaWalk walk;
    if(!prime_va_begin(&walk,orbis_va_base,orbis_va_end)) return 4;
    for(struct amdgpu_va *v=orbis_va_list;v;v=v->next) {
        if(!prime_va_add(&walk,v->base,v->size,v->retired)) {
            fprintf(stderr,"Reused reservation overlaps a still-live range: base=%" PRIx64 " previous_end=%" PRIx64 "\n",v->base,walk.cursor);
            return 5;
        }
    }
    prime_va_finish(&walk);
    struct amdgpu_va *extra=NULL;
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,0,16384,0,&at,&extra,0)!=-EINVAL) return 6;
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,16384,16384,b->base,&at,&extra,0)!=-EINVAL) return 7;
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,UINT64_MAX,16384,0,&at,&extra,0)!=-ENOMEM) return 8;
    // A final reservation can reach the end without a following guard page.
    const uint64_t tail=orbis_va_end-(replacement->base+replacement->size);
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,tail,16384,0,&at,&extra,0)) return 9;
    if(extra->base+extra->size!=orbis_va_end) return 10;
    // The old hole still cannot hold 32 KiB PLUS the mandatory guard.
    if(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,32768,16384,0,&at,&extra,0)!=-ENOMEM) return 11;
    puts("Reused VA hole preserves the live allocation and its reserved guard gap");
    while(orbis_va_list) {a=orbis_va_list;orbis_va_list=a->next;free(a);}
}'''
        out=ROOT/'build/mesa-va-regression';out.mkdir(exist_ok=True)
        unit=out/'allocator.c';unit.write_text(prelude+body+test)
        program=out/'allocator'
        subprocess.run(['clang-19','-std=c11','-D_GNU_SOURCE','-O2',
                        '-I'+str(ROOT/'renderer/orbis'),str(unit),'-o',str(program)],check=True)
        result=subprocess.run([program],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertIn('preserves the live allocation',result.stdout)
    def test_fragmented_arena_retries_once_only_after_completed_retirement(self):
        source=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        body=source.split('int\nac_drm_va_range_alloc(',1)[1].split('\nint\nac_drm_va_range_free(',1)[0]
        prelude=r'''
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "va_statistics.h"
typedef void ac_drm_device;
enum amdgpu_gpu_va_range { UNUSED_RANGE };
struct amdgpu_va { uint64_t base,size;struct amdgpu_va *next;bool retired;uint64_t retire_seq; };
typedef struct amdgpu_va* amdgpu_va_handle;
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define AMDGPU_VA_RANGE_HIGH 2
#define AMDGPU_VA_RANGE_32_BIT 1
#define mesa_logi(...) ((void)0)
#define mesa_loge(...) ((void)0)

static int orbis_va_lock;
static struct amdgpu_va *orbis_va_list;
static uint64_t orbis_va_base=0x220000000ull,orbis_va_end=0x220200000ull;
static void simple_mtx_lock(int *p) {assert(!*p);*p=1;}
static void simple_mtx_unlock(int *p) {assert(*p);*p=0;}
static uint64_t orbis_page_size(void) {return 16384;}
static uint64_t orbis_align_up(uint64_t v,uint64_t a) {return (v+a-1)&~(a-1);}
static bool orbis_budget(unsigned *p,unsigned max) {(void)p;(void)max;return false;}

static unsigned recovery_calls;
static bool wait_complete,release_ranges;
bool prime_orbis_va_snapshot(struct PrimeOrbisVaStats *out) {
 struct PrimeOrbisVaWalk walk;assert(!orbis_va_lock);
 assert(prime_va_begin(&walk,orbis_va_base,orbis_va_end));
 for(struct amdgpu_va *v=orbis_va_list;v;v=v->next)
  assert(prime_va_add(&walk,v->base,v->size,v->retired));
 prime_va_finish(&walk);*out=walk.stats;return true;
}
bool prime_orbis_reclaim_for_allocation(uint64_t timeout) {
 assert(!orbis_va_lock && timeout==500000000ull);++recovery_calls;
 if(!wait_complete) return false;
 if(release_ranges) {
  for(struct amdgpu_va **p=&orbis_va_list;*p;) {
   struct amdgpu_va *v=*p;
   if(v->retired) {*p=v->next;free(v);} else p=&v->next;
  }
 }
 return true;
}
int ac_drm_va_range_alloc('''
        test=r'''
int main(void) {
 unsetenv("ORBIS_VA_NO_REUSE");unsetenv("ORBIS_NO_VA_GAP");
 struct amdgpu_va *blocks[6],*large=NULL;uint64_t at;
 for(unsigned i=0;i<6;++i)
  assert(!ac_drm_va_range_alloc(NULL,UNUSED_RANGE,256*1024,16384,0,&at,&blocks[i],0));
 const uint64_t hole=blocks[1]->base;
 blocks[1]->retired=true;blocks[2]->retired=true;
 // Enough bytes exist after retirement, but no currently free gap fits.
 wait_complete=false;
 assert(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,512*1024,16384,0,&at,&large,0)==-ENOMEM);
 assert(recovery_calls==1 && blocks[1]->retired && blocks[2]->retired);
 // A successful wait with no recovered range must not loop forever.
 wait_complete=true;release_ranges=false;recovery_calls=0;
 assert(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,512*1024,16384,0,&at,&large,0)==-ENOMEM);
 assert(recovery_calls==1);
 // Actual recovery returns the combined retired gap while live blocks survive.
 release_ranges=true;recovery_calls=0;
 assert(!ac_drm_va_range_alloc(NULL,UNUSED_RANGE,512*1024,16384,0,&at,&large,0));
 assert(recovery_calls==1 && at==hole && !large->retired);
 struct PrimeOrbisVaStats stats;assert(prime_orbis_va_snapshot(&stats));
 assert(stats.retired==0 && stats.ranges==5);
 for(unsigned i=0;i<6;++i) if(i!=1 && i!=2) assert(!blocks[i]->retired);
 recovery_calls=0;
 assert(ac_drm_va_range_alloc(NULL,UNUSED_RANGE,UINT64_MAX,16384,0,&at,&large,0)==-ENOMEM);
 assert(recovery_calls==0); // A true live-allocation OOM has nothing to reclaim.
 while(orbis_va_list) {struct amdgpu_va *v=orbis_va_list;orbis_va_list=v->next;free(v);}
 puts("Fragmented allocation recovers only completed retired space; timeout and no-progress retries remain bounded");
}
'''
        out=ROOT/'build/mesa-va-pressure';out.mkdir(exist_ok=True)
        unit=out/'allocator.c';unit.write_text(prelude+body+test)
        program=out/'allocator'
        subprocess.run(['gcc-14','-std=c11','-D_GNU_SOURCE','-O2','-fsanitize=address,undefined',
                        '-I'+str(ROOT/'renderer/orbis'),str(unit),'-o',str(program)],check=True)
        result=subprocess.run([program],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertIn('timeout and no-progress retries remain bounded',result.stdout)
if __name__=='__main__':unittest.main()
