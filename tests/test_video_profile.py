from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class VideoProfileTests(unittest.TestCase):
    def test_flip_observer_preserves_arguments_results_and_ownership(self):
        with tempfile.TemporaryDirectory() as temp:
            headers=Path(temp)/'orbis';headers.mkdir()
            (headers/'libkernel.h').write_text('#include <stdint.h>\nuint64_t sceKernelGetProcessTime(void);\n')
            binary=Path(temp)/'test'
            subprocess.run(['gcc-14','-std=c11','-O2','-fsanitize=address,undefined','-I'+temp,
                str(ROOT/'platform/ps4/video_profile.c'),str(ROOT/'tests/video_profile.c'),'-o',str(binary)],check=True)
            run=subprocess.run([binary],capture_output=True,text=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
