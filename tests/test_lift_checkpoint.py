from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class LiftCheckpointTests(unittest.TestCase):
    def test_real_codec_preserves_game_and_rejects_wrong_inputs(self):
        build=ROOT/'build/lift-checkpoint-tests'
        build.mkdir(exist_ok=True)
        include='-I'+str(ROOT/'game/MetroidPrimePort/platform/include')
        subprocess.run(['clang++-19','-std=c++20','-O2',include,'-Dmain=prime_lift_checkpoint_main',
                        '-c',str(ROOT/'tools/prepare_lift_checkpoint.cpp'),'-o',str(build/'tool.o')],check=True)
        subprocess.run(['clang++-19','-std=c++20','-O2',include,str(build/'tool.o'),
                        str(ROOT/'tests/lift_checkpoint.cpp'),
                        str(ROOT/'game/MetroidPrimePort/platform/port_savestate_format.cpp'),
                        '-o',str(build/'run')],check=True)
        with tempfile.TemporaryDirectory(dir=build) as temp:
            subprocess.run([build/'run',temp],check=True)
if __name__=='__main__': unittest.main()
