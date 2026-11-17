"""The player ZIP must preserve package bits and expose only verified helper files."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build_release_bundle import build_bundle
from package_menu_movies import TARGETS


def digest(data):
    return hashlib.sha256(data).hexdigest()


class ReleaseBundleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / "package"
        (self.package / "licenses").mkdir(parents=True)
        self.bits = b"verified package fixture"
        (self.package / "game.pkg").write_bytes(self.bits)
        self.receipt = {"stage": "packaged_and_host_verified", "metadata": {
            "APP_VER": "00.39", "TITLE_ID": "PRME00013"}, "package": "game.pkg",
            "bytes": len(self.bits), "package_sha256": digest(self.bits),
            "commercial_game_data_included": False, "private_artwork_provenance": "do not ship"}
        self.write_receipt()
        (self.package / "licenses/NOTICE.txt").write_text("Component notice")
        (self.package / "PRIVATE-JOURNAL.md").write_text("do not ship")
        self.docs = self.root / "source"
        (self.docs / "docs/images").mkdir(parents=True)
        (self.docs / "docs/CONTROLS.md").write_text("Options pauses.")
        (self.docs / "docs/images/ps4-controls.svg").write_text("<svg/>")
        self.helper = self.root / "helper.zip"
        self.output = self.root / "download.zip"
        self.make_helper()

    def write_receipt(self):
        (self.package / "receipt.json").write_text(json.dumps(self.receipt))

    def make_helper(self, extra=None):
        payload = {"GamePrep.exe": b"MZfixture", "README.txt": b"Helper guide",
                   "licenses/LICENSE.txt": b"Helper notice",
                   "licenses/component-0.1.dist-info-LICENSE": b"Component notice"}
        manifest = {"version": "1.0.0", "game_data_included": False,
                    "files": {name: {"bytes": len(data), "sha256": digest(data)}
                              for name, data in payload.items()}}
        raw = json.dumps(manifest).encode()
        with zipfile.ZipFile(self.helper, "w") as archive:
            for name, data in payload.items():
                archive.writestr("ProjectPrime-GamePrep/" + name, data)
            archive.writestr("ProjectPrime-GamePrep/manifest.json", raw)
            if extra:
                archive.writestr(*extra)
        self.helper.with_suffix(".json").write_text(json.dumps({
            "package": self.helper.name, "bytes": self.helper.stat().st_size,
            "sha256": digest(self.helper.read_bytes()), "manifest_sha256": digest(raw)}))

    def build(self, version="00.39"):
        return build_bundle(version, self.package, self.helper, self.output, self.docs)

    def test_one_download_contains_exact_package_and_ready_exe(self):
        result = self.build()
        self.assertTrue(result["zip_readback_verified"])
        with zipfile.ZipFile(self.output) as archive:
            prefix = "Project-Prime-PS4-00.39/"
            self.assertEqual(archive.read(prefix + "PS4/Metroid-Prime-PS4-00.39.pkg"), self.bits)
            self.assertEqual(archive.read(prefix + "GamePrep/GamePrep.exe"), b"MZfixture")
            self.assertFalse(any(name.endswith("ps4-controls.svg") for name in archive.namelist()))
            self.assertFalse(any(name.endswith(".zip") or "JOURNAL" in name or "receipt" in name
                                 for name in archive.namelist()))
            self.assertNotIn(b"private_artwork", archive.read(prefix + "manifest.json"))
            for line in archive.read(prefix + "SHA256SUMS.txt").decode().splitlines():
                expected, name = line.split("  ", 1)
                self.assertEqual(digest(archive.read(prefix + name)), expected)

    def test_package_corruption_and_wrong_version_are_rejected(self):
        with self.assertRaises(ValueError):
            self.build("00.40")
        (self.package / "game.pkg").write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.output.exists())

    def test_public_one_point_zero_names_keep_native_version(self):
        self.receipt["metadata"]["APP_VER"] = "01.00"
        self.write_receipt()
        result = self.build("01.00")
        self.assertEqual(result["version"], "01.00")
        with zipfile.ZipFile(self.output) as archive:
            prefix = "Metroid-Prime-PS4-1.0/"
            name = prefix + "PS4/Metroid-Prime-PS4-1.0.pkg"
            self.assertEqual(archive.read(name), self.bits)
            self.assertIn(b"Metroid Prime PS4 1.0", archive.read(prefix + "README.txt"))

    def test_only_verified_installed_menu_movies_are_accepted(self):
        self.receipt.update(commercial_game_data_included=True,
                            asset_install_layout="direct_app0_menu_movies",
                            bundled_menu_movies={"scale": 1.5, "width": 960, "height": 720,
                                                 "files": {name: "a"*64 for name in TARGETS}},
                            extracted_files={name: "a"*64 for name in TARGETS})
        self.write_receipt()
        result = self.build()
        self.assertTrue(result["game_data_included"])

    def test_incomplete_or_other_game_payload_is_rejected(self):
        self.receipt["commercial_game_data_included"] = True
        self.write_receipt()
        with self.assertRaises(ValueError): self.build()

        self.receipt.update(asset_install_layout="direct_app0_menu_movies",
                            bundled_menu_movies={"scale": 1.5, "width": 960, "height": 720,
                                                 "files": {name: "a"*64 for name in TARGETS}},
                            extracted_files={name: "b"*64 for name in TARGETS})
        self.write_receipt()
        with self.assertRaises(ValueError): self.build()

    def test_screenshot_tour_cannot_enter_public_download(self):
        self.receipt['screenshot_tour_embedded'] = True
        self.write_receipt()
        with self.assertRaises(ValueError): self.build()
        self.assertFalse(self.output.exists())

    def test_helper_traversal_or_unlisted_file_is_rejected(self):
        for path in ("ProjectPrime-GamePrep/../../escape", "ProjectPrime-GamePrep/extra.iso"):
            with self.subTest(path=path):
                self.make_helper((path, b"unwanted"))
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(self.output.exists())
                self.assertFalse((self.root / "escape").exists())

    def test_corrupt_helper_and_existing_output_are_preserved(self):
        self.helper.write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            self.build()
        self.output.write_bytes(b"existing download")
        with self.assertRaises(ValueError):
            self.build()
        self.assertEqual(self.output.read_bytes(), b"existing download")


if __name__ == "__main__":
    unittest.main()
