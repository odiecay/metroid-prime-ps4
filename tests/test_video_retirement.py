from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class VideoRetirementTests(unittest.TestCase):
    def test_native_close_guard_failures_do_not_return_to_image_destruction(self):
        with tempfile.TemporaryDirectory() as d:
            headers=Path(d)/'orbis';headers.mkdir()
            (headers/'VideoOut.h').write_text('''#include <stdint.h>
typedef struct { int32_t numFlipPending; } OrbisVideoOutFlipStatus;
int32_t sceVideoOutGetFlipStatus(int32_t,OrbisVideoOutFlipStatus*);
''')
            (headers/'libkernel.h').write_text('''#include <stdint.h>
uint64_t sceKernelGetProcessTime(void);
int32_t sceKernelUsleep(uint32_t);
''')
            output=Path(d)/'test'
            subprocess.run(['/usr/lib/llvm-19/bin/clang','-O2','-Wall','-Wextra','-Werror',
                '-Dprime_ps4_exit_to_shell=prime_test_exit','-I'+d,ROOT/'platform/ps4/video.c',
                ROOT/'tests/video_retirement.c','-o',output],check=True)
            result=subprocess.run([output],check=True,stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,text=True,timeout=5)
            self.assertIn('timeout and close error passed',result.stdout)
