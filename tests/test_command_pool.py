from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
class CommandPoolTests(unittest.TestCase):
    def test_bounded_storage_lifetimes_and_frame_handoff(self):
        build=ROOT/'build/command-pool-host';build.mkdir(exist_ok=True)
        program=build/'run'
        subprocess.run(['g++-14','-std=c++20','-O2','-pthread','-fsanitize=address,undefined',
                        '-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib'),
                        str(ROOT/'tests/command_pool.cpp'),'-o',str(program)],check=True)
        result=subprocess.run([program],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertIn('zero new vector allocations',result.stdout)
if __name__=='__main__':unittest.main()
