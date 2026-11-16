"""Preserve the unfogged sentinel and nested generator restoration."""
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
class ParticleFogTests(unittest.TestCase):
    def test_three_state_api_and_nested_restoration(self):
        game=ROOT/'game/MetroidPrimePort';program=ROOT/'build/particle-fog-scope-host'
        if not (game/'platform/include/port_fx_debug.h').exists():self.skipTest('bootstrap game source first')
        program.parent.mkdir(exist_ok=True)
        subprocess.run(['clang++-19','-std=c++20','-O1','-g','-DTARGET_PC',
            '-I'+str(game/'platform/include'),'-I'+str(game/'extern/aurora/include'),
            str(ROOT/'tests/particle_fog_scope.cpp'),'-o',str(program)],check=True)
        subprocess.run([program],check=True)
if __name__=='__main__':unittest.main()
