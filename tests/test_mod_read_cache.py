import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('prepare_mod_read_cache', Path(__file__).resolve().parents[1]/'tools/prepare_mod_read_cache.py')
CACHE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CACHE)


class ModReadCacheTests(unittest.TestCase):
    def test_prioritises_particle_graph_and_bounds_memory(self):
        with tempfile.TemporaryDirectory() as temp:
            mod = Path(temp)
            # Only one of these fits a 256-byte total cache budget. The
            # particle wins even though the texture is lexically earlier.
            (mod/'00000001.TXTR').write_bytes(b't'*100)
            part = mod/'00000002.PART'
            part.write_bytes(b'p'*100)
            (mod/'00000003.dds').write_bytes(b'd'*100)
            before = part.read_bytes()
            report = CACHE.prepare(mod, 256)
            self.assertEqual(report['types'], {'PART': 1})
            self.assertLessEqual(report['retained_bytes'], 256)
            self.assertEqual(part.read_bytes(), before)
            first = (mod/report['file']).read_bytes()
            self.assertEqual(report, CACHE.prepare(mod, 256))
            self.assertEqual(first, (mod/report['file']).read_bytes())

    def test_pack_metadata_references_exact_payloads(self):
        with tempfile.TemporaryDirectory() as temp:
            mod = Path(temp)
            assets = {'01234567.PART': b'p'*41, 'ABCDEF01.SWHC': b's'*17}
            for name, data in assets.items():
                (mod/name).write_bytes(data)
            report = CACHE.prepare(mod, 4096)
            blob = (mod/report['file']).read_bytes()
            magic, count, table_end, total = CACHE.HEADER.unpack_from(blob)
            self.assertEqual(magic, b'MPRC0001')
            self.assertEqual(total, len(blob))
            offset = CACHE.HEADER.size
            seen = {}
            for _ in range(count):
                length, flags, size, start, digest = CACHE.ENTRY.unpack_from(blob, offset)
                offset += CACHE.ENTRY.size
                name = blob[offset:offset+length].decode()
                offset += length
                self.assertEqual(flags, 0)
                self.assertGreaterEqual(start, table_end)
                self.assertEqual(start % 32, 0)
                seen[name] = blob[start:start+size]
                self.assertEqual(CACHE.fnv64(seen[name]), digest)
            self.assertEqual(offset, table_end)
            self.assertEqual(seen, assets)

    def test_symlink_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            mod = Path(temp)
            (mod/'source.PART').write_bytes(b'p')
            (mod/'01234567.PART').symlink_to(mod/'source.PART')
            with self.assertRaises(ValueError):
                CACHE.prepare(mod)


if __name__ == '__main__':
    unittest.main()
