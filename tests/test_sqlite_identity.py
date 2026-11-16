"""Exercise the real Unix VFS with mismatched native path/handle inode fields."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class SqliteIdentityTests(unittest.TestCase):
    def test_native_identity_and_real_rename(self):
        program=ROOT/'build/sqlite-identity-host'
        source=ROOT/'build/game-native/_deps/sqlite3-src'
        subprocess.run(['clang-19','-std=c11','-O0','-D_GNU_SOURCE','-pthread',
                        '-I'+str(source),str(ROOT/'platform/ps4/sqlite.c'),
                        str(ROOT/'tests/sqlite_identity.c'),str(source/'sqlite3.c'),
                        '-ldl','-lm','-o',str(program)],check=True)
        with tempfile.TemporaryDirectory() as folder:
            result=subprocess.run([program,folder],check=True,stdout=subprocess.PIPE,text=True,timeout=15)
        self.assertIn('DBMOVED reproduced',result.stdout)
        self.assertIn('detects real rename/deletion',result.stdout)
if __name__=='__main__':unittest.main()
