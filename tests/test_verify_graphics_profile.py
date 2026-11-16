from pathlib import Path
import importlib.util
import json
import struct
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('verify_profiles',ROOT/'tools/verify_graphics_profile.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def dds():
    out=bytearray(148+48);out[:4]=b'DDS '
    struct.pack_into('<I',out,4,124)
    struct.pack_into('<II',out,12,4,4);struct.pack_into('<I',out,28,3)
    out[84:88]=b'DX10';struct.pack_into('<IIIII',out,128,98,3,0,1,0)
    return out


def marker(profile,manifest):
    manifest.setdefault('profile','test');manifest.setdefault('upstream','fixture');manifest.setdefault('import_version',520)
    path=profile/'prime-assets.txt'
    path.write_text(f"PROJECT_PRIME_ASSETS_V1\n{manifest['profile']}\n{manifest['upstream']}\n{manifest['import_version']}\n")
    manifest['runtime_marker_sha256']=m.sha(path)
    rows=[(r['path'],r['size']) for r in manifest['files']]
    stamp=profile/'mods/remastered-models/.import-version'
    if stamp.exists(): rows.append(('mods/remastered-models/.import-version',stamp.stat().st_size))
    index=profile/'prime-assets-index.txt';index.write_text(''.join(f'{size} {p}\n' for p,size in sorted(rows)))
    manifest['runtime_index_sha256']=m.sha(index)


class ProfileVerifyTests(unittest.TestCase):
    def test_bc_mip_chain_checks_detect_truncation_and_unsupported_format(self):
        self.assertEqual(m.dds_info(dds())['mips'],3)
        with self.assertRaises(ValueError): m.dds_info(dds()[:-1])
        data=dds();struct.pack_into('<I',data,128,28)
        with self.assertRaisesRegex(ValueError,'format'): m.dds_info(data)

    def test_manifest_detects_corrupt_and_unexpected_payloads(self):
        with tempfile.TemporaryDirectory() as tmp:
            profile=Path(tmp);mod=profile/'mods/remastered-models';mod.mkdir(parents=True)
            asset=mod/'12345678.TXTR';asset.write_bytes(b'original')
            stamp=mod/'.import-version';stamp.write_text('520\ntest\n')
            manifest=dict(schema=1,selection_version=1,profile='test',import_version=520,
                          source_stamp_sha256=m.sha(stamp),asset_bytes=8,asset_files=1,
                          files=[dict(path='mods/remastered-models/12345678.TXTR',size=8,sha256=m.sha(asset))])
            marker(profile,manifest)
            (profile/'profile.json').write_text(json.dumps(manifest))
            self.assertEqual(m.verify(profile)['files'],1)
            asset.write_bytes(b'changed!')
            with self.assertRaisesRegex(ValueError,'checksum'): m.verify(profile)
            asset.write_bytes(b'original');(mod/'prod.keys').write_text('never include this')
            with self.assertRaisesRegex(ValueError,'unexpected'): m.verify(profile)
            (mod/'prod.keys').unlink();(profile/'prime-assets-index.txt').write_text('')
            with self.assertRaisesRegex(ValueError,'index'): m.verify(profile)

    def test_manifest_traversal_cannot_read_outside_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            profile=Path(tmp);(profile/'mods').mkdir()
            manifest=dict(schema=1,selection_version=1,files=[dict(path='../prod.keys',size=0,sha256='')])
            marker(profile,manifest)
            (profile/'profile.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'unsafe'): m.verify(profile)
