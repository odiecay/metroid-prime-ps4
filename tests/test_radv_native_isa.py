"""Check the exact RADV scalar helpers and optional native command-TU pair.

The scalar checks exercise compiler lowering, not GPU execution or frame speed.
Set PRIME_RADV_ISA_BASE/PRIME_RADV_ISA_TUNED to compare the real native objects.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-msse4.2', '-mpopcnt', '-mcx16', '-mtune=btver2', '-mno-avx']
CLANG = '/usr/bin/clang-18'
SOURCE = ROOT / 'external/mesa-ps4/src/amd/vulkan/radv_cmd_buffer.c'
OUT = Path(os.environ.get('PRIME_RADV_ISA_TEST_DIR', ROOT / 'build/radv-isa-tests'))


def body(source, name):
    """Extract a named production function, including its parameter list."""
    start = source.index(name + '(')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PRELUDE = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <strings.h>
#define MAX_SAMPLE_LOCATIONS 32
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define util_bitcount __builtin_popcount
typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { int32_t x, y; } VkOffset2D;
typedef struct { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct { float x, y; } VkSampleLocationEXT;
struct radv_sample_locations_state {
 unsigned per_pixel; VkExtent2D grid_size; uint32_t count;
 VkSampleLocationEXT locations[MAX_SAMPLE_LOCATIONS];
};
'''

MAIN = r'''
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern unsigned base_radv_instance_rate_prolog_index(unsigned, uint32_t);
extern unsigned tune_radv_instance_rate_prolog_index(unsigned, uint32_t);
extern VkRect2D base_radv_scissor_from_viewport(const float *, const float *);
extern VkRect2D tune_radv_scissor_from_viewport(const float *, const float *);
extern void base_radv_convert_user_sample_locs(const struct radv_sample_locations_state *, uint32_t, uint32_t, VkOffset2D *);
extern void tune_radv_convert_user_sample_locs(const struct radv_sample_locations_state *, uint32_t, uint32_t, VkOffset2D *);
extern void base_round_values(float, float *);
extern void tune_round_values(float, float *);
static uint32_t rng = 0x98a17731;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; return rng ^= rng << 5; }
static float from_bits(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }
int main(void) {
 uint64_t masks=0, scissors=0, samples=0, raw=0;
 // Exhaust every valid attribute mask and number of attributes, including all
 // disjoint masks: no zero-mask/count-minus-one undefined table access.
 for (unsigned n=1; n<=16; ++n) for (uint32_t m=1; m<(1u<<n); ++m) {
  assert(base_radv_instance_rate_prolog_index(n,m)==tune_radv_instance_rate_prolog_index(n,m)); ++masks;
 }
 const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
 const uint32_t bits[] = {0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,
  0x00800000,0x80800000,0x3f000000,0xbf000000,0x3f7fffff,0x3f800000,
  0x3f800001,0xbf800001,0x4affffff,0xcaffffff,0x7f800000,0xff800000,
  0x7fc00001,0xffc12345,0x7f800001,0xff800001};
 for (unsigned mode=0;mode<4;++mode) {
  assert(!fesetround(modes[mode]));
  // Direct floating lowering control: no NaN/infinity-to-integer conversion.
  // Payload/signed-zero bits must survive the same way in both versions.
  for (unsigned i=0;i<sizeof(bits)/sizeof(bits[0]);++i) {
   float a[2],b[2]; base_round_values(from_bits(bits[i]),a); tune_round_values(from_bits(bits[i]),b);
   assert(!memcmp(a,b,sizeof(a))); ++raw;
  }
  for (unsigned i=0;i<50000;++i) {
   // Finite viewport transform inputs; every integer conversion is in range.
   float s[3]={(int)(next()%8193)/16.f,(int)(next()%8193)/16.f,1.f};
   float t[3]={1024.f+(int)(next()%8193)/16.f,1024.f+(int)(next()%8193)/16.f,0.f};
   if (i&1) s[1]=-s[1];
   if ((i&255)==0) s[0]=from_bits(i&256?0x80000000:1);
   VkRect2D a=base_radv_scissor_from_viewport(s,t),b=tune_radv_scissor_from_viewport(s,t);
   assert(!memcmp(&a,&b,sizeof(a))); ++scissors;
   struct radv_sample_locations_state state={0};
   state.per_pixel=1u<<(i%4); state.grid_size=(VkExtent2D){2,2}; state.count=4*state.per_pixel;
   for(unsigned j=0;j<state.count;++j) {
    state.locations[j].x=(next()%65537)/65536.f; state.locations[j].y=(next()%65537)/65536.f;
   }
   state.locations[0].x=from_bits(i&1?0x80000000:1); // valid zero/subnormal positions
   VkOffset2D aa[8]={0},bb[8]={0};
   base_radv_convert_user_sample_locs(&state,i%2,(i/2)%2,aa);
   tune_radv_convert_user_sample_locs(&state,i%2,(i/2)%2,bb);
   assert(!memcmp(aa,bb,sizeof(aa))); ++samples;
  }
 }
 printf("{\"attribute_masks\":%llu,\"viewport_scissors\":%llu,\"sample_locations\":%llu,\"raw_rounding_controls\":%llu}\n",
  (unsigned long long)masks,(unsigned long long)scissors,(unsigned long long)samples,(unsigned long long)raw);
 return 0;
}
'''

NATIVE_MAIN = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Push { uint32_t type; const void *next; uint64_t layout; uint32_t stages,offset,size; const void *values; };
struct Rect { int32_t x,y; uint32_t width,height; };
extern unsigned base_radv_instance_rate_prolog_index(unsigned,uint32_t);
extern unsigned tune_radv_instance_rate_prolog_index(unsigned,uint32_t);
extern void base_radv_CmdPushConstants2(void *,const struct Push *);
extern void tune_radv_CmdPushConstants2(void *,const struct Push *);
extern void base_radv_CmdSetScissor(void *,uint32_t,uint32_t,const struct Rect *);
extern void tune_radv_CmdSetScissor(void *,uint32_t,uint32_t,const struct Rect *);
extern void base_radv_CmdSetLineWidth(void *,float);
extern void tune_radv_CmdSetLineWidth(void *,float);
static uint32_t rng=0x51badfac;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; return rng ^= rng << 5; }
void *base_memcpy(void *d,const void *s,size_t n) { return memcpy(d,s,n); }
void *tune_memcpy(void *d,const void *s,size_t n) { return memcpy(d,s,n); }
int main(void) {
 // Exact native layout sizeof(radv_cmd_buffer)=20688. The setters under test
 // touch only raw state/dirty fields; no allocator, device, dispatch or GPU.
 enum { Size=20688, Guard=64 };
 unsigned char *aa=malloc(Size+2*Guard),*bb=malloc(Size+2*Guard); assert(aa&&bb);
 memset(aa,0xa5,Size+2*Guard);memset(bb,0xa5,Size+2*Guard);
 unsigned char *a=aa+Guard,*b=bb+Guard;memset(a,0,Size);memset(b,0,Size);
 uint32_t data[64];uint64_t masks=0;
 for(unsigned n=1;n<=16;++n)for(uint32_t m=1;m<(1u<<n);++m){
  assert(base_radv_instance_rate_prolog_index(n,m)==tune_radv_instance_rate_prolog_index(n,m));++masks;
 }
 for(unsigned i=0;i<100000;++i) {
  for(unsigned j=0;j<64;++j)data[j]=next();
  // Push constants are untyped bits: include NaNs, infinities and signed zero.
  data[0]=i&1?0x80000000:0x7f800001;data[1]=0x7fc12345;data[2]=1;
  uint32_t off=(next()%64)*4,len=(next()%((256-off)/4+1))*4;
  struct Push p={0,0,0,1u<<(i%6),off,len,data};
  base_radv_CmdPushConstants2(a,&p);tune_radv_CmdPushConstants2(b,&p);
  struct Rect r[16];for(unsigned j=0;j<16;++j)r[j]=(struct Rect){(int)(next()%1024),(int)(next()%1024),next()%2048,next()%2048};
  unsigned first=next()%16,count=next()%(17-first);
  base_radv_CmdSetScissor(a,first,count,r);tune_radv_CmdSetScissor(b,first,count,r);
  float width;memcpy(&width,&data[0],4);
  base_radv_CmdSetLineWidth(a,width);tune_radv_CmdSetLineWidth(b,width);
  assert(!memcmp(aa,bb,Size+2*Guard));
  for(unsigned j=0;j<Guard;++j)assert(aa[j]==0xa5&&aa[Guard+Size+j]==0xa5);
 }
 printf("{\"native_attribute_masks\":%llu,\"native_state_sequences\":100000,\"guard_bytes\":64}\n",(unsigned long long)masks);
 free(aa);free(bb);return 0;
}
'''


class RadvNativeIsaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)

    def scalar_contract(self, sanitized):
        source = SOURCE.read_text()
        functions = '\n'.join([
            'unsigned ' + body(source, 'radv_instance_rate_prolog_index'),
            'VkRect2D ' + body(source, 'radv_scissor_from_viewport'),
            'void ' + body(source, 'radv_convert_user_sample_locs'),
            'void round_values(float x,float out[2]) { out[0]=floorf(x);out[1]=ceilf(x); }',
        ])
        directory = OUT / ('san' if sanitized else 'normal')
        directory.mkdir(exist_ok=True)
        flags = ['-O3', '-std=c11', '-fno-math-errno', '-fno-trapping-math']
        if sanitized:
            flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
        objects = []
        for prefix, extra in [('base_', []), ('tune_', FLAGS)]:
            text = functions
            for name in ['radv_instance_rate_prolog_index', 'radv_scissor_from_viewport', 'radv_convert_user_sample_locs', 'round_values']:
                text = text.replace(name, prefix + name)
            path = directory / (prefix + 'helpers.c')
            path.write_text(PRELUDE + text)
            obj = path.with_suffix('.o')
            subprocess.run([CLANG, *flags, *extra, '-c', str(path), '-o', str(obj)], check=True)
            objects.append(str(obj))
        main = directory / 'main.c'
        main.write_text(PRELUDE + MAIN)
        binary = directory / 'contract'
        link = ['-fno-sanitize-link-runtime', '-lasan', '-lubsan'] if sanitized else []
        subprocess.run([CLANG, *flags, str(main), *objects, '-lm', *link, '-o', str(binary)], check=True)
        env = dict(os.environ)
        if sanitized:
            env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                       ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result = subprocess.check_output([str(binary)], text=True, env=env)
        (directory / 'result.json').write_text(result)
        self.assertEqual(json.loads(result)['viewport_scissors'], 200000)

    def test_actual_scalar_helpers_normal(self):
        self.scalar_contract(False)

    def test_actual_scalar_helpers_asan_ubsan(self):
        self.scalar_contract(True)

    @unittest.skipUnless(os.environ.get('PRIME_RADV_ISA_BASE') and os.environ.get('PRIME_RADV_ISA_TUNED'),
                         'native pair supplied by the exact-SDK overlay build')
    def test_actual_native_object_pair(self):
        objects = []
        for prefix, name in [('base_', 'PRIME_RADV_ISA_BASE'), ('tune_', 'PRIME_RADV_ISA_TUNED')]:
            obj = OUT / (prefix + 'native.o')
            subprocess.run(['/usr/bin/llvm-objcopy-18', '--prefix-symbols=' + prefix,
                            os.environ[name], str(obj)], check=True)
            objects.append(str(obj))
        main = OUT / 'native-main.c';main.write_text(NATIVE_MAIN)
        binary = OUT / 'native-contract'
        subprocess.run([CLANG, '-O2', '-ffunction-sections', '-fdata-sections', str(main), *objects,
                        '-Wl,--gc-sections', '-o', str(binary)], check=True)
        result = subprocess.check_output([str(binary)], text=True)
        (OUT / 'native-result.json').write_text(result)
        self.assertEqual(json.loads(result)['native_state_sequences'], 100000)

    @unittest.skipUnless(os.environ.get('PRIME_RADV_ISA_TUNED'), 'native tuned object supplied')
    def test_native_instruction_and_dependency_contract(self):
        obj = os.environ['PRIME_RADV_ISA_TUNED']
        asm = subprocess.check_output(['/usr/bin/llvm-objdump-18', '-d', '--no-show-raw-insn', obj], text=True)
        self.assertNotRegex(asm, r'%[yz]mm\d')
        mnemonics = re.findall(r'^\s*[0-9a-f]+:\s+([a-zA-Z0-9.]+)', asm, re.M)
        self.assertFalse([m for m in mnemonics if m.startswith('v')])
        self.assertIn('popcntl', mnemonics)
        undefined = subprocess.check_output(['/usr/bin/llvm-nm-18', '-u', obj], text=True)
        self.assertNotIn('__popcount', undefined)
        if os.environ.get('PRIME_RADV_ISA_BASE'):
            baseline = subprocess.check_output(['/usr/bin/llvm-nm-18', '-u', os.environ['PRIME_RADV_ISA_BASE']], text=True)
            symbols = lambda text: {line.split()[-1] for line in text.splitlines()}
            self.assertFalse(symbols(undefined) - symbols(baseline))


if __name__ == '__main__':
    unittest.main()
