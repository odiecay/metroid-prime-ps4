from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
class StdioRedirectTests(unittest.TestCase):
    def test_shared_offsets_errors_and_flagged_requests(self):
        output=ROOT/'build/stdio-redirect-host'
        subprocess.run(['/usr/lib/llvm-19/bin/clang','-O2','-Wall','-Wextra','-Werror',
            ROOT/'platform/ps4/stdio.c',ROOT/'tests/stdio_redirect.c','-o',output],check=True)
        result=subprocess.run([output],check=True,stdout=subprocess.PIPE,text=True,timeout=5)
        self.assertIn('shared offsets and failures passed',result.stdout)
