"""Verify the bounded nine-movie payload and its decoder receipts."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from package_menu_movies import NAMES, TARGETS, verify_menu_movies, verify_hd_font


class MenuPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        video = self.root/'hd-menu/Video'
        video.mkdir(parents=True)
        header = bytearray(80)
        struct.pack_into('>6I', header, 0, 0x54485000, 0x10000, 4, 0, 1106231951, 1)
        struct.pack_into('>I', header, 32, 48)
        struct.pack_into('>I', header, 48, 1)
        struct.pack_into('>II', header, 68, 960, 720)
        rows = []
        for name in NAMES:
            path = video/name
            path.write_bytes(header)
            rows.append({'output': 'hd-menu/Video/'+name, 'file_bytes': 80,
                         'output_sha256': hashlib.sha256(header).hexdigest(),
                         'frames': 1, 'fps_bits': 1106231951, 'all_frames_decoded': True,
                         'plane_guards_intact': True, 'audio_components': 0,
                         'audio_bytes': 0, 'audio_crc32': '00000000',
                         'source_letterbox_preserved': True, 'aspect_stretched': False})
        self.receipt = {'scale': 1.5, 'width': 960, 'height': 720, 'all_movies_decoded': True,
                        'display_framing_unchanged': True, 'clips': rows,
                        'total_movie_bytes': 720, 'total_frames': 9}
        self.write_receipt()

    def write_receipt(self):
        (self.root/'menu-movies.json').write_text(json.dumps(self.receipt))

    def test_exact_direct_payload(self):
        files, receipt = verify_menu_movies(self.root)
        self.assertEqual(set(files), TARGETS)
        self.assertEqual((receipt['bytes'], receipt['frames']), (720, 9))
        self.assertFalse(receipt['first_launch_extraction'])

    def test_selected_three_times_resolution_preserves_extent(self):
        self.receipt.update(scale=3, width=1920, height=1440)
        for row in self.receipt['clips']:
            path = self.root/row['output']
            data = bytearray(path.read_bytes())
            struct.pack_into('>II', data, 68, 1920, 1440)
            path.write_bytes(data)
            row['output_sha256'] = hashlib.sha256(data).hexdigest()
        self.write_receipt()
        _, receipt = verify_menu_movies(self.root)
        self.assertEqual((receipt['scale'], receipt['width'], receipt['height']), (3, 1920, 1440))

    def test_hd_font_header_and_metrics_are_bounded(self):
        data = struct.pack('<4sIIIffI', b'SDFT', 1, 4, 4, 1., .2, 1)
        data += struct.pack('<I9f', ord('H'), 0., 1., 1., 1., 0., 1., 1., 0., 1.) + bytes(16)
        path = self.root/'deface.sdfont'
        path.write_bytes(data)
        self.assertEqual(verify_hd_font(path)['glyphs'], 1)
        path.write_bytes(data[:-1])
        with self.assertRaises(ValueError): verify_hd_font(path)
        bad = bytearray(data)
        struct.pack_into('<f', bad, 32, float('nan'))
        path.write_bytes(bad)
        with self.assertRaises(ValueError): verify_hd_font(path)

    def test_missing_extra_linked_and_changed_payload(self):
        extra = self.root/'hd-menu/Video/extra.thp'
        extra.write_bytes(b'extra')
        with self.assertRaises(ValueError): verify_menu_movies(self.root)
        extra.unlink()
        path = self.root/'hd-menu/Video'/NAMES[0]
        original = path.read_bytes()
        path.unlink()
        with self.assertRaises(ValueError): verify_menu_movies(self.root)
        other = self.root/'other'
        other.write_bytes(original)
        path.symlink_to(other)
        with self.assertRaises(ValueError): verify_menu_movies(self.root)
        path.unlink()
        path.write_bytes(original[:-1])
        with self.assertRaises(ValueError): verify_menu_movies(self.root)

    def test_bad_decode_audio_framing_or_aggregate_rejected(self):
        for field, bad in [('all_frames_decoded', False), ('plane_guards_intact', False),
                           ('audio_components', 1), ('audio_crc32', '12345678'),
                           ('aspect_stretched', True), ('fps_bits', 0), ('frames', 2)]:
            previous = self.receipt['clips'][0][field]
            self.receipt['clips'][0][field] = bad
            self.write_receipt()
            with self.subTest(field=field), self.assertRaises(ValueError):
                verify_menu_movies(self.root)
            self.receipt['clips'][0][field] = previous
        self.receipt['total_movie_bytes'] += 1
        self.write_receipt()
        with self.assertRaises(ValueError): verify_menu_movies(self.root)

    def test_hash_consistent_but_wrong_resolution_header_rejected(self):
        path = self.root/self.receipt['clips'][0]['output']
        data = bytearray(path.read_bytes())
        struct.pack_into('>I', data, 68, 1920)
        path.write_bytes(data)
        self.receipt['clips'][0]['output_sha256'] = hashlib.sha256(data).hexdigest()
        self.write_receipt()
        with self.assertRaises(ValueError): verify_menu_movies(self.root)


if __name__ == '__main__': unittest.main()
