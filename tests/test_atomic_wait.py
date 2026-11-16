"""Exercise native runtime wait-channel races without a driver or console."""
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
class AtomicWaitTests(unittest.TestCase):
    def test_early_notify_collisions_and_handoffs(self):
        program=ROOT/'build/atomic-wait-channels-host';program.parent.mkdir(exist_ok=True)
        subprocess.run(['clang++-19','-std=c++20','-O2','-g','-pthread',
                        str(ROOT/'tests/atomic_wait_channels.cpp'),'-o',str(program)],check=True)
        result=subprocess.run([program],check=True,stdout=subprocess.PIPE,text=True,timeout=20)
        self.assertIn('8000 handoffs passed',result.stdout)
if __name__=='__main__':unittest.main()
