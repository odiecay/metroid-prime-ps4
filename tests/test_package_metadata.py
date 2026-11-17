"""Reject corrupt package metadata before reporting a verified local package."""
import importlib.util
from pathlib import Path
import struct
import sys
import contextlib
import io
import json
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('prime_package', ROOT / 'tools/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

def fixture():
    keys = b'TITLE_ID\0APP_VER\0'
    value = b'PRME00001\0' + b'00.03\0'
    header = struct.pack('<5I', 0x46535000, 0x101, 52, 52 + len(keys), 2)
    return bytearray(header + struct.pack('<HHIII', 0, 0x204, 10, 10, 0) +
                     struct.pack('<HHIII', 9, 0x204, 6, 6, 10) + keys + value)

class PackageMetadataTests(unittest.TestCase):
    def test_game_release_uses_metroid_identity(self):
        self.assertEqual(package.game_release_identity('01.00'),
                         ('MPRM00001', 'Metroid Prime', 'METROIDPRIMEPS4'))
        self.assertEqual(package.game_release_identity('00.55'),
                         ('PRME00013', 'Metroid Prime', 'PROJECTPRIME0025'))
    def test_game_artwork_must_be_selected_before_build_inputs_are_touched(self):
        args = ['package.py', '--kind', 'game', '--elf', 'unused.elf', '--oelf', 'unused.oelf',
                '--eboot', 'unused.bin', '--version', '00.56', '--out', 'unused-output']
        class ReachedInputValidation(Exception):
            pass
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'upstream.lock.json').write_text(json.dumps({}))
            with patch.object(package, 'ROOT', root), patch.object(package, 'startup_modules',
                    side_effect=ReachedInputValidation) as inputs:
                with patch.object(sys, 'argv', args), contextlib.redirect_stderr(io.StringIO()) as error:
                    with self.assertRaises(SystemExit) as stopped:
                        package.main()
                self.assertEqual(stopped.exception.code, 2)
                self.assertIn('artwork must not be silently omitted', error.getvalue())
                inputs.assert_not_called()
                for selection in (['--private-artwork', 'chosen-art'], ['--no-artwork'], ['--verify-existing']):
                    with self.subTest(selection=selection), patch.object(sys, 'argv', args + selection):
                        with self.assertRaises(ReachedInputValidation):
                            package.main()
                self.assertFalse((root/'unused-output').exists())

    def test_valid_utf8_fields(self):
        self.assertEqual(package.sfo_values(fixture()), {'TITLE_ID': 'PRME00001', 'APP_VER': '00.03'})

    def test_corrupt_table_bounds(self):
        for length in (0, 4, 19, 40, 60):
            with self.subTest(length=length), self.assertRaises((ValueError, struct.error)):
                package.sfo_values(fixture()[:length])

    def test_payload_overflow_rejected(self):
        data = fixture()
        struct.pack_into('<I', data, 32, 0xffffffff)
        with self.assertRaises(ValueError):
            package.sfo_values(data)

    def test_duplicate_keys_rejected(self):
        data = fixture()
        struct.pack_into('<H', data, 36, 0)
        with self.assertRaises(ValueError):
            package.sfo_values(data)

    def test_extractor_uses_table_index(self):
        self.assertEqual(package.entry_indices('0x3EB0  0x434  00000000  9       PARAM_SFO\n'
                                               '0x62F0  0x8927A 00000000 14       ICON0_PNG'),
                         {'PARAM_SFO': 9, 'ICON0_PNG': 14})

if __name__ == '__main__':
    unittest.main()
