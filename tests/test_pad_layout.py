"""Verify exact stick neutral/endpoints and SDL's packed virtual button slots."""
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class PadLayoutTests(unittest.TestCase):
    def test_axes_and_every_virtual_button_slot(self):
        program=ROOT/'build/pad-layout-host';program.parent.mkdir(exist_ok=True)
        subprocess.run(['clang-19','-std=c11','-O1',str(ROOT/'tests/pad_layout.c'),'-o',str(program)],check=True)
        subprocess.run([program],check=True)
if __name__=='__main__':unittest.main()
