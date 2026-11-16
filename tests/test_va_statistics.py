"""Verify the exact statistics helper used under the native VA allocation lock."""
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class VaStatisticsTests(unittest.TestCase):
    def test_fragmentation_and_retired_ranges(self):
        program=ROOT/'build/va-statistics-host'
        subprocess.run(['gcc-14','-std=c11','-O2','-fsanitize=address,undefined',
                        str(ROOT/'tests/va_statistics.c'),'-o',str(program)],check=True)
        output=subprocess.check_output([program],text=True,timeout=15)
        self.assertIn('fragmentation, reclamation and overflow checks passed',output)
if __name__=='__main__':unittest.main()
