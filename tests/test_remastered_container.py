"""Reject malformed PC import containers before the actual upstream reader runs."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare_remastered',ROOT/'tools/prepare_remastered_archive.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class RemasteredContainerTests(unittest.TestCase):
    def check_container(self,name='asset.nca',offset=0,length=4,truncate=False):
        names=name.encode()+b'\0'
        data=b'PFS0'+struct.pack('<III',1,len(names),0)+struct.pack('<QQII',offset,length,0,0)+names+b'data'
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'image.nsp';p.write_bytes(data[:20] if truncate else data)
            return module.pfs0(p)
    def test_valid_table(self):
        self.assertEqual(self.check_container(),[{'name':'asset.nca','offset':0,'bytes':4}])
    def test_truncated_table(self):
        with self.assertRaises(ValueError):self.check_container(truncate=True)
    def test_outside_and_overflowing_ranges(self):
        for offset,length in ((4,1),(0,5),(2**64-1,4),(1,2**64-1)):
            with self.subTest(offset=offset,length=length),self.assertRaises(ValueError):
                self.check_container(offset=offset,length=length)
    def test_path_escape(self):
        with self.assertRaises(ValueError):self.check_container(name='../asset.nca')
if __name__=='__main__':unittest.main()
