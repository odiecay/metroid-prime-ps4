from pathlib import Path
import os
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]

class GxDrawProfileTests(unittest.TestCase):
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
