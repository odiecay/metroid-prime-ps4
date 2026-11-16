from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class ModInventoryTests(unittest.TestCase):
    def test_cached_installed_files_and_invalid_catalog_fallback(self):
        build=ROOT/'build/mod-inventory-v0027';build.mkdir(exist_ok=True)
        binary=build/'inventory'
        subprocess.run(['g++-14','-std=c++20','-O1','-fsanitize=address,undefined',
            '-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),
            str(ROOT/'tests/mod_inventory.cpp'),'-o',str(binary)],check=True)
        with tempfile.TemporaryDirectory() as tmp:
            result=subprocess.run([binary,tmp],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        print(result.stdout,end='')
