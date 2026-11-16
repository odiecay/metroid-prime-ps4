from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ResumeCookieTests(unittest.TestCase):
    def test_changed_truncated_and_unmarked_resume_is_not_automatically_offered(self):
        output=ROOT/'build/resume-cookie-test'
        subprocess.run(['clang++-19','-std=c++20','-O2',
                        '-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),
                        str(ROOT/'tests/resume_cookie.cpp'),'-o',str(output)],check=True)
        subprocess.run([output],check=True)
if __name__=='__main__':unittest.main()
