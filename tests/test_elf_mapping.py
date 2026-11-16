"""Fault-inject actual generated native images, including writable BSS loss."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from verify_elf import verify
from native_tls_fixture import build_fixture

class ElfMappingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Relink with the current project script; the archived foundation
        # images predate the corrected native TLS alignment.
        cls.elf,cls.oelf,cls.eboot=build_fixture('mapping',17)
        cls.data=cls.oelf.read_bytes()
        start=struct.unpack_from('<Q',cls.data,32)[0]
        size,count=struct.unpack_from('<HH',cls.data,54)
        cls.rw=next(start+i*size for i in range(count)
                    if struct.unpack_from('<II',cls.data,start+i*size)==(1,6))
        cls.interp=next(start+i*size for i in range(count)
                       if struct.unpack_from('<I',cls.data,start+i*size)[0]==3)
        cls.eh_frame=next(start+i*size for i in range(count)
                         if struct.unpack_from('<I',cls.data,start+i*size)[0]==0x6474e550)

    def reject(self,offset,fmt,value,message):
        data=bytearray(self.data);struct.pack_into(fmt,data,offset,value)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'corrupt.oelf';path.write_bytes(data)
            with self.assertRaisesRegex(ValueError,message):verify(self.elf,path)

    def test_real_native_conversion_preserves_mappings(self):
        result=verify(self.elf,self.oelf,self.eboot)
        self.assertTrue(result['all_load_bytes_mapped'] and result['self_tls_verified'])

    def test_changed_entry_point_is_rejected(self):
        self.reject(24,'<Q',1,'entry point differs')

    def test_bss_truncation_is_rejected(self):
        self.reject(self.rw+40,'<Q',0,'loses original LOAD')

    def test_writable_storage_cannot_become_read_only(self):
        self.reject(self.rw+4,'<I',4,'not mapped RW')

    def test_unaligned_mapping_is_rejected(self):
        address=struct.unpack_from('<Q',self.data,self.rw+16)[0]
        self.reject(self.rw+16,'<Q',address+1,'not 16 KiB aligned')

    def test_duplicate_interpreter_is_rejected(self):
        self.reject(self.eh_frame,'<I',3,'exactly one interpreter')

    def test_missing_interpreter_is_rejected(self):
        self.reject(self.interp,'<I',0,'exactly one interpreter')

    def test_corrupt_interpreter_path_is_rejected(self):
        offset=struct.unpack_from('<Q',self.data,self.interp+8)[0]
        self.reject(offset,'<B',ord('!'),'interpreter pathname')

if __name__=='__main__':unittest.main()
