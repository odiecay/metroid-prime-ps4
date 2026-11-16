from pathlib import Path
import importlib.util
import json
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_room_assets as rooms
from verify_graphics_profile import verify


def model(texture):
    # Only the material dependency table is relevant to the selector.
    data = bytearray(64)
    struct.pack_into('>II', data, 0, 0xDEADBABE, 2)
    struct.pack_into('>III', data, 36, 1, 1, 8)
    return bytes(data) + struct.pack('>II', 1, texture)


def dds():
    data = bytearray(148 + 24)
    data[:4] = b'DDS '
    struct.pack_into('<I', data, 4, 124)
    struct.pack_into('<II', data, 12, 4, 4)
    struct.pack_into('<I', data, 28, 3)
    data[84:88] = b'DX10'
    struct.pack_into('<IIIII', data, 128, 71, 3, 0, 1, 0)
    return bytes(data)


class RoomAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.private = Path(self.temp.name)
        self.source = self.private / 'conversion'
        (self.source / 'roomgeo').mkdir(parents=True)
        (self.source / 'roomenv').mkdir()
        self.out = self.private / 'selected'
        self.room = 0xAABBCCDD
        (self.source / '.import-version').write_text('520\n')
        (self.source / 'roomgeo/AABBCCDD.roomgeo').write_bytes(
            struct.pack('<4sIII12f', b'MPRG', 1, 1, 0x11111111,
                        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))
        (self.source / 'roomenv/AABBCCDD.roomenv').write_bytes(
            struct.pack('<4sI4fII', b'MPEV', 1, 0, 0, 0, 0, 0, 0))
        (self.source / 'roomgeo/lods.bin').write_bytes(
            struct.pack('<4sIIIIfI', b'RLOD', 1, 1, 0x11111111, 1, 100, 0x22222222))
        (self.source / 'roomenv/brdf.lut').write_bytes(bytes(256))
        for ident, texture in ((0x11111111, 0x10101010), (0x22222222, 0x20202020), (0x33333333, 0x30303030)):
            (self.source / f'roomgeo/{ident:08X}.CMDL').write_bytes(model(texture))
            (self.source / f'roomgeo/{texture:08X}.TXTR').write_bytes(b'synthetic TXTR')
            (self.source / f'roomgeo/{texture:08X}.dds').write_bytes(dds())

    def tearDown(self):
        self.temp.cleanup()

    def prepare(self, **kw):
        return rooms.prepare(self.source, self.out, [self.room], private_root=self.private, **kw)

    def test_dependency_closure_includes_lod_texture_and_matching_environment(self):
        result = self.prepare()
        self.assertEqual(result['models_including_lods'], 2)
        self.assertEqual(result['textures'], 2)
        self.assertEqual(result['native_dds_maps'], 2)
        self.assertTrue((self.out / 'mods/remastered-models/roomgeo/22222222.CMDL').is_file())
        self.assertFalse((self.out / 'mods/remastered-models/roomgeo/33333333.CMDL').exists())
        self.assertTrue((self.out / 'mods/remastered-models/roomenv/AABBCCDD.roomenv').is_file())
        self.assertFalse(result['hardware_tested'])
        self.assertEqual(result['selected_bytes'], sum(row['size'] for row in result['files']))

    def test_missing_lod_model_fails_without_partial_output(self):
        (self.source / 'roomgeo/22222222.CMDL').unlink()
        with self.assertRaisesRegex(ValueError, 'missing model'):
            self.prepare()
        self.assertFalse(self.out.exists())

    def test_missing_material_texture_fails(self):
        (self.source / 'roomgeo/20202020.TXTR').unlink()
        with self.assertRaisesRegex(ValueError, 'missing texture'):
            self.prepare()

    def test_bad_native_dds_is_rejected(self):
        (self.source / 'roomgeo/20202020.dds').write_bytes(dds()[:-1])
        with self.assertRaisesRegex(ValueError, 'payload length'):
            self.prepare()

    def test_budget_failure_does_not_leave_partial_output(self):
        with self.assertRaisesRegex(ValueError, 'exceed budget'):
            self.prepare(max_added_bytes=1)
        self.assertFalse(self.out.exists())

    def test_missing_environment_and_symlink_rejected(self):
        env = self.source / 'roomenv/AABBCCDD.roomenv'
        env.unlink()
        with self.assertRaisesRegex(ValueError, 'missing or unsafe'):
            self.prepare()
        env.symlink_to(self.source / 'roomenv/brdf.lut')
        with self.assertRaisesRegex(ValueError, 'missing or unsafe'):
            self.prepare()

    def test_real_parser_rejects_bad_geometry(self):
        (self.source / 'roomgeo/AABBCCDD.roomgeo').write_bytes(b'bad geometry')
        with self.assertRaises(Exception):
            self.prepare()
        self.assertFalse(self.out.exists())

    def test_existing_output_is_never_overwritten(self):
        self.out.mkdir()
        sentinel = self.out / 'save-me'
        sentinel.write_text('preserved')
        with self.assertRaisesRegex(ValueError, 'refusing existing'):
            self.prepare()
        self.assertEqual(sentinel.read_text(), 'preserved')


if __name__ == '__main__':
    unittest.main()
