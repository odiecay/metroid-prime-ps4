from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CandidateProfileTests(unittest.TestCase):
    def test_isolation_copy_and_rejection(self):
        build=ROOT/'build/candidate-profile-tests';build.mkdir(exist_ok=True)
        binary=build/'test'
        subprocess.run(['g++-14','-std=c++20','-O1','-pthread','-fsanitize=address,undefined',
            str(ROOT/'tests/candidate_profile.cpp'),str(ROOT/'platform/ps4/candidate_profile.cpp'),
            '-o',str(binary)],check=True)
        with tempfile.TemporaryDirectory() as temp:
            run=subprocess.run([binary,temp],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
