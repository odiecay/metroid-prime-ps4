from pathlib import Path
import os
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]

class GxDrawProfileTests(unittest.TestCase):
    def test_actual_cpu_clock_policy_defaults_off_and_caches_explicit_opt_in(self):
        def function(source, marker):
            start=source.index(marker);pos=source.index('{',start);depth=1;end=pos+1
            while depth:
                depth+=(source[end]=='{')-(source[end]=='}');end+=1
            return source[start:end]
        gx=ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx'
        helper=function((gx/'command_processor.cpp').read_text(),'draw_profile::Clock draw_prep_cpu_clock()')
        env=function((gx.parent/'internal.hpp').read_text(),'inline bool env_flag(')
        out=ROOT/'build/gx-draw-profile-tests';out.mkdir(exist_ok=True)
        code=out/'cpu-policy.cpp';exe=out/'cpu-policy'
        code.write_text('''#include "draw_profile.hpp"
#include <cassert>
#include <cstdlib>
#include <initializer_list>
using namespace aurora::gx::fifo;
static unsigned calls=0;
static uint64_t cpu() noexcept { ++calls;return 123; }
static uint64_t wall() noexcept { return 456; }
namespace trace { draw_profile::Clock cpu_clock=cpu; }
'''+env+'\n'+helper+'''
int main(int argc,char** argv) {
 assert(argc==2);const bool expected=argv[1][0]=='1';
 const auto first=draw_prep_cpu_clock();assert((first!=nullptr)==expected);
 setenv("MP_GX_DRAW_PREP_CPU",expected?"0":"1",1);
 assert(draw_prep_cpu_clock()==first);
 draw_profile::Metrics metrics;
 {draw_profile::Draw draw(metrics,true,wall,draw_prep_cpu_clock());draw.complete();}
 const auto f=metrics.take_frame();assert(calls==(expected?2u:0u));
 assert(f.cpuClockReads==(expected?2u:0u)&&f.wallClockReads==2);
}
''')
        subprocess.run(['g++-14','-std=c++20','-O2','-Wall','-Wextra','-Werror',
                        '-DPRIME_TRACE_TEST','-I'+str(gx),str(code),'-o',str(exe)],check=True)
        for value in (None,'','0','false','OFF','no','1'):
            case=dict(os.environ);case.pop('MP_GX_DRAW_PREP_CPU',None)
            if value is not None:case['MP_GX_DRAW_PREP_CPU']=value
            subprocess.run([exe,'1' if value=='1' else '0'],env=case,check=True,timeout=5)

    def test_bounded_sampling_clock_and_accounting_policy(self):
        out=ROOT/'build/gx-draw-profile-tests';out.mkdir(exist_ok=True)
        exe=out/'policy'
        subprocess.run(['g++-14','-std=c++20','-O2','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        '-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx'),
                        str(ROOT/'tests/gx_draw_profile.cpp'),'-o',str(exe)],check=True)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
        result=subprocess.run([exe],env=env,capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        print(result.stdout,end='')
    def test_aggregate_emission_stays_outside_draw_work(self):
        source=(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx/command_processor.cpp').read_text()
        start=source.index('void clear_draw_cache() noexcept {')
        self.assertNotIn('"GX prep ',source[:start])
        self.assertEqual(source.count('MP_GX_DRAW_PREP_PROFILE'),1)
        for label in ('GX prep draws','GX prep samples','GX prep sampled wall ns','GX prep sampled CPU ns'):
            self.assertEqual(source.count('"'+label+'"'),1)

if __name__=='__main__':unittest.main()
