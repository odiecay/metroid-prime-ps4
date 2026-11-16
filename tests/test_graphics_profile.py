from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class GraphicsProfileTests(unittest.TestCase):
    def test_migration_and_user_choices(self):
        program=ROOT/'build/graphics-profile-host'
        subprocess.run(['clang-19','-std=c11','-O2',str(ROOT/'platform/ps4/graphics_profile.c'),
                        str(ROOT/'tests/graphics_profile.c'),'-o',str(program)],check=True)
        self.assertIn('retained user choices',subprocess.check_output([program],text=True))
if __name__=='__main__':unittest.main()
