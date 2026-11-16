from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FrameMetricsTests(unittest.TestCase):
    def test_percentiles_stalls_and_overflow(self):
        program=ROOT/'build/frame-metrics-host'
        subprocess.run(['clang++-19','-std=c++20','-O2',str(ROOT/'tests/frame_metrics.cpp'),'-o',str(program)],check=True)
        self.assertIn('retain stalls',subprocess.check_output([program],text=True))
if __name__=='__main__':unittest.main()
