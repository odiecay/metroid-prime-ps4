import os
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class BenchmarkPlayLogicTests(unittest.TestCase):
    def test_actual_route_and_input_logic(self):
        out=Path(os.environ.get('PRIME_PLAY_LOGIC_BUILD_DIR',str(ROOT/'build/benchmark-play-logic')))
        out.mkdir(parents=True,exist_ok=True);exe=out/'run'
        command=['/usr/lib/llvm-19/bin/clang++','-std=c++20','-O2','-Wall','-Wextra','-Werror',
                 '-I'+str(ROOT/'platform/ps4'),str(ROOT/'tests/benchmark_play_logic.cpp'),'-o',str(exe)]
        sanitized=os.environ.get('PRIME_BENCHMARK_SANITIZE')=='1'
        if sanitized:
            command+=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-link-runtime',
                      '-fno-omit-frame-pointer','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        subprocess.run(command,check=True,capture_output=True,text=True)
        env=dict(os.environ)
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
        result=subprocess.run([str(exe)],env=env,check=True,capture_output=True,text=True)
        self.assertIn('2880 normal-input phase cases passed',result.stdout)
if __name__=='__main__':unittest.main()
