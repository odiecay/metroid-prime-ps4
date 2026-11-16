from pathlib import Path
import struct,subprocess,tempfile,unittest,zlib,gzip
ROOT=Path(__file__).resolve().parents[1]
def bundle(files):
    data=b'PRIMEASSET1\n'+struct.pack('<IQ',len(files),sum(len(v) for _,v in files))
    for name,value in files:
        n=name.encode();data+=struct.pack('<HQI',len(n),len(value),zlib.crc32(value))+n+value
    return data
FILES=[('profile.json',b'{}'),('prime-assets.txt',b'marker'),('prime-assets-index.txt',b'index'),
       ('mods/remastered-models/.import-version',b'520\n'),('mods/remastered-models/00000001.CMDL',b'converted model')]
class BundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=ROOT/'build/asset-bundle-tests';cls.build.mkdir(exist_ok=True)
        main=cls.build/'main.cpp'
        main.write_text('#include "asset_bundle.hpp"\n#include <exception>\nint main(int n,char** v){if(n!=3)return 2;try{prime::unpack_asset_bundle(v[1],v[2]);return 0;}catch(const std::exception&){return 1;}}\n')
        cls.exe=cls.build/'test'
        subprocess.run(['g++-14','-std=c++20','-O1','-fsanitize=address,undefined',
            '-I'+str(ROOT/'platform/ps4'),str(main),str(ROOT/'platform/ps4/asset_bundle.cpp'),'-lz','-o',str(cls.exe)],check=True)
    def run_input(self,data):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);src=root/'assets';dst=root/'unpacked';src.write_bytes(data)
            run=subprocess.run([self.exe,src,dst],capture_output=True,text=True,timeout=20)
            content={str(p.relative_to(dst)):p.read_bytes() for p in dst.rglob('*') if p.is_file()} if dst.exists() else None
            self.assertFalse(Path(str(dst)+'.unpacking').exists())
            return run.returncode,content
    def test_exact_resources_are_atomically_published(self):
        code,files=self.run_input(bundle(FILES));self.assertEqual(code,0);self.assertEqual(files,dict(FILES))
    def test_truncation_does_not_publish_partial_assets(self):
        code,files=self.run_input(bundle(FILES)[:-1]);self.assertEqual(code,1);self.assertIsNone(files)
    def test_corruption_does_not_publish_partial_assets(self):
        raw=bytearray(bundle(FILES));raw[-1]^=1
        code,files=self.run_input(raw);self.assertEqual(code,1);self.assertIsNone(files)
    def test_traversal_is_rejected(self):
        code,files=self.run_input(bundle(FILES+[('mods/remastered-models/../../../save.raw',b'bad')]))
        self.assertEqual(code,1);self.assertIsNone(files)
    def test_incomplete_catalog_is_rejected(self):
        code,files=self.run_input(bundle(FILES[1:]));self.assertEqual(code,1);self.assertIsNone(files)
    def test_existing_destination_is_preserved(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);src=root/'assets';dst=root/'unpacked';dst.mkdir();(dst/'save').write_bytes(b'keep')
            src.write_bytes(bundle(FILES));run=subprocess.run([self.exe,src,dst],capture_output=True,timeout=20)
            self.assertEqual(run.returncode,0);self.assertEqual((dst/'save').read_bytes(),b'keep');self.assertEqual(len(list(dst.iterdir())),1)

    def test_compressed_transport_preserves_exact_resources(self):
        code,files=self.run_input(gzip.compress(bundle(FILES),compresslevel=1,mtime=0))
        self.assertEqual(code,0);self.assertEqual(files,dict(FILES))
    def test_corrupted_compressed_transport_is_rejected(self):
        raw=bytearray(gzip.compress(bundle(FILES),compresslevel=1,mtime=0));raw[-4]^=1
        code,files=self.run_input(raw);self.assertEqual(code,1);self.assertIsNone(files)
