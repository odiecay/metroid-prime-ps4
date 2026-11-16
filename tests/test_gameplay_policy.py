from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class GameplayPolicyTests(unittest.TestCase):
    def test_default_migration_and_snapshot_bounds(self):
        program=ROOT/'build/gameplay-policy-host'
        subprocess.run(['gcc-14','-std=c11','-O2','-fsanitize=address,undefined',
                        str(ROOT/'platform/ps4/gameplay_policy.c'),str(ROOT/'tests/gameplay_policy.c'),
                        '-o',str(program)],check=True)
        self.assertIn('reject void/nonfinite',subprocess.check_output([program],text=True))
    def test_original_setting_excludes_actual_alternate_mover_gate(self):
        unit=ROOT/'build/original-mover-gate.cpp';program=unit.with_suffix('')
        unit.write_text('#include "port_remastered_ground_policy.h"\n#include <cassert>\nint main(){using namespace PortRemasteredGround;assert(UsesRemasteredMover({true,true,true}));assert(!UsesRemasteredMover({true,true,false}));}\n')
        subprocess.run(['clang++-19','-std=c++20','-O2','-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),str(unit),'-o',str(program)],check=True)
        subprocess.run([program],check=True)
if __name__=='__main__':unittest.main()
