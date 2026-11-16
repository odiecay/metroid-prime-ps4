from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class GPUReadbackStateTests(unittest.TestCase):
    def test_epoch_state_is_atomic_across_delayed_callback_and_device_reset(self):
        build=ROOT/'build/gpu-readback-state';build.mkdir(exist_ok=True)
        binary=build/'run'
        subprocess.run(['g++-14','-std=c++20','-O2','-pthread','-fsanitize=address,undefined',
            '-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib'),
            str(ROOT/'tests/gpu_readback_state.cpp'),'-o',str(binary)],check=True)
        run=subprocess.run([binary],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('10000 callback/reset races',run.stdout)
