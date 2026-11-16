"""A full-image checksum match is required, beyond recognizing a disc header."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_data import verified_redump_record

class DataVerificationTests(unittest.TestCase):
    def setUp(self):
        self.result='Redump: Metroid Prime (USA) ✅\nCRC32 : 852b658c ✅\nSHA-1 : ac20c744db18fdf0339f37945e880708fd317231 ✅\n'
    def test_clean_pinned_database_match(self):
        self.assertEqual(verified_redump_record(self.result)['sha1'],'ac20c744db18fdf0339f37945e880708fd317231')
    def test_recognized_title_with_bad_checksum_is_rejected(self):
        with self.assertRaises(ValueError):verified_redump_record(self.result.replace('852b658c ✅','00000000 ❌'))
    def test_no_match_is_not_promoted_to_verified(self):
        with self.assertRaises(ValueError):verified_redump_record('Disc: GM8E01\nSHA-1 : ac20c744db18fdf0339f37945e880708fd317231\n')
    def test_other_game_is_rejected(self):
        with self.assertRaises(ValueError):verified_redump_record(self.result.replace('Metroid Prime','Another Game'))

if __name__=='__main__':unittest.main()
