"""Actual native TLS link, OELF conversion and packaged SELF ABI regression."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from verify_elf import parse,program_headers,self_tls,verify
from native_tls_fixture import build_fixture

def symbols(path):
    image=Path(path).read_bytes();parsed=parse(path)
    table=next(s for name,s in parsed['sections'] if name=='.symtab')
    names=parsed['sections'][table[6]][1]
    strings=image[names[4]:names[4]+names[5]];result={}
    for offset in range(table[4],table[4]+table[5],table[9]):
        name,info,other,section,value,size=struct.unpack_from('<IBBHQQ',image,offset)
        name=strings[name:].split(b'\0',1)[0].decode()
        result[name]=(value,size)
    return result

def local_exec_offset(path):
    image=Path(path).read_bytes();parsed=parse(path)
    address,_=symbols(path)['prime_tls_local_exec_offset']
    load=next(p for p in parsed['programs'] if p[0]==1 and p[3]<=address<p[3]+p[5])
    offset=load[2]+address-load[3];code=image[offset:offset+8]
    if code[:3]!=b'\x48\xc7\xc0' or code[7]!=0xc3:
        raise AssertionError('Expected actual linked x86-64 TPOFF32 immediate fixture')
    return struct.unpack_from('<i',code,3)[0]

class NativeTlsImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.images=[build_fixture('layout-'+str(n),n) for n in (1,9,17,25,33,41)]
        cls.elf,cls.oelf,cls.eboot=cls.images[0]

    def test_actual_linker_offsets_match_converted_runtime_for_varying_tls_sizes(self):
        residues=set()
        for elf,oelf,eboot in self.images:
            with self.subTest(elf=elf):
                result=verify(elf,oelf,eboot);tls=result['tls'][0]
                self.assertEqual(tls['alignment'],32)
                residues.add(tls['memsz']%32)
                canary=symbols(elf)['prime_tls_canary'][0]
                self.assertEqual(local_exec_offset(elf),canary-tls['local_exec_span'])
                self.assertTrue(result['tls_template_preserved'] and result['self_tls_verified'])
        # Exercise both aligned and non-aligned sizes, rather than repairing
        # just the current game's byte count with special-case padding.
        self.assertGreater(len(residues),1)

    def test_old_alignment_change_reproduces_zero_canary_without_losing_template(self):
        with tempfile.TemporaryDirectory() as directory:
            script=Path(directory)/'old.ld'
            script.write_text((ROOT/'platform/ps4/orbis-native.ld').read_text().replace(
                '.tls : ALIGN(0x20) {','.tls : {'))
            elf,oelf,eboot=build_fixture('old-layout',25,script)
        original=parse(elf);converted=parse(oelf)
        a,b=original['tls'][0],converted['tls'][0]
        self.assertNotEqual(a['local_exec_span'],b['local_exec_span'])
        self.assertEqual(a['template_sha256'],b['template_sha256'])
        self.assertEqual(b['template_sha256'],self_tls(eboot)[0]['template_sha256'])
        tls=next(p for p in original['programs'] if p[0]==7)
        runtime_index=b['local_exec_span']+local_exec_offset(elf)
        self.assertNotEqual(runtime_index,symbols(elf)['prime_tls_canary'][0])
        self.assertEqual(struct.unpack_from('<I',elf.read_bytes(),tls[2]+runtime_index)[0],0)
        with self.assertRaisesRegex(ValueError,'changes PT_TLS alignment'):verify(elf,oelf,eboot)

    def reject_oelf(self,mutate,message):
        data=bytearray(self.oelf.read_bytes());mutate(data)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'corrupt.oelf';path.write_bytes(data)
            with self.assertRaisesRegex(ValueError,message):verify(self.elf,path,self.eboot)

    def test_conversion_alignment_change_is_rejected(self):
        def mutate(data):
            p=program_headers(data);i=next(i for i,p in enumerate(p) if p[0]==7)
            phoff=struct.unpack_from('<Q',data,32)[0]
            struct.pack_into('<Q',data,phoff+i*56+48,64)
        self.reject_oelf(mutate,'changes PT_TLS alignment')

    def test_changed_initializer_bytes_are_rejected(self):
        def mutate(data):
            tls=next(p for p in program_headers(data) if p[0]==7)
            data[tls[2]]^=1
        self.reject_oelf(mutate,'initialized template bytes')

    def test_changed_self_template_bytes_are_rejected(self):
        data=bytearray(self.eboot.read_bytes());count=struct.unpack_from('<H',data,24)[0]
        programs=program_headers(data,32+count*32)
        tls=next(p for p in programs if p[0]==7)
        i,load=next((i,p) for i,p in enumerate(programs) if p[0] in (1,0x61000010)
                    and p[3]<=tls[3] and tls[3]+tls[5]<=p[3]+p[5])
        entries=[struct.unpack_from('<QQQQ',data,32+j*32) for j in range(count)]
        entry=next(e for e in entries if e[0]&(1<<11) and (e[0]>>20)&0xffff==i)
        data[entry[1]+tls[3]-load[3]]^=1
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'corrupt.bin';path.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'SELF.*initialized template bytes'):
                verify(self.elf,self.oelf,path)

if __name__=='__main__':unittest.main()
