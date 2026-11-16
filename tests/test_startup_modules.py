"""Prevent packages from silently omitting or substituting startup PRXs."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from package import startup_modules

class StartupModuleTests(unittest.TestCase):
    def fixture(self,root):
        # Host-only structural fixture. Real packaging requires upstream pins.
        data=bytearray(128);data[:4]=bytes.fromhex('4f153d1d')
        data[32:38]=b'\x7fELF\x02\x01';struct.pack_into('<HH',data,48,0xfe18,62)
        items=[]
        for name in ('libc.prx','libSceFios2.prx'):
            path=root/'external'/name;path.parent.mkdir(exist_ok=True);path.write_bytes(data)
            items.append({'path':str(path.relative_to(root)),
                          'sha256':hashlib.sha256(data).hexdigest()})
        return {'native_startup_modules':items}

    def test_required_pair_is_verified(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);lock=self.fixture(root)
            self.assertEqual(set(startup_modules(lock,root)),{'libc.prx','libSceFios2.prx'})

    def test_missing_module_is_fatal(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);lock=self.fixture(root);(root/'external/libc.prx').unlink()
            with self.assertRaisesRegex(ValueError,'Missing required'):startup_modules(lock,root)

    def test_empty_catalog_is_fatal(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError,'startup modules are required'):
                startup_modules({},Path(d))

    def test_wrong_module_bytes_are_fatal(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);lock=self.fixture(root);(root/'external/libc.prx').write_bytes(b'wrong')
            with self.assertRaisesRegex(ValueError,'differs from pinned'):startup_modules(lock,root)

if __name__=='__main__':unittest.main()
