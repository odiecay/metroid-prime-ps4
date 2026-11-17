#!/usr/bin/env python3
"""Make one player download from an already verified PKG and GamePrep ZIP.

Does not build, install or launch the game. Copies only an explicit player-file
allowlist; local build receipts and engineering records stay outside the ZIP.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import tempfile
import zipfile
from package_menu_movies import TARGETS

ROOT = Path(__file__).resolve().parents[1]
SOURCE_URL = "https://github.com/odiecay/metroid-prime-ps4"


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def player_version(version):
    major, minor = map(int, version.split("."))
    return f"{major}.{minor}" if major else version


def safe_name(name):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or "\\" in name or any(
        part in ("", ".", "..") for part in name.split("/")
    ) or ":" in name:
        raise ValueError("Unsafe archive path: " + name)
    return path


def verified_package(folder, version):
    receipt_path = folder / "receipt.json"
    if not receipt_path.is_file():
        receipt_path = folder / "build-receipt.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    menu = receipt.get("bundled_menu_movies")
    menu_only = bool(menu and receipt.get("asset_install_layout") == "direct_app0_menu_movies" and
                     not receipt.get("installed_asset_profile") and not receipt.get("remastered_bundle_sha256") and
                     menu.get("scale") == 1.5 and (menu.get("width"), menu.get("height")) == (960, 720) and
                     set(menu.get("files", {})) == TARGETS and all(
                         receipt.get("extracted_files", {}).get(name) == digest
                         for name, digest in menu["files"].items()))
    if receipt.get("stage") != "packaged_and_host_verified" or \
       receipt.get("metadata", {}).get("APP_VER") != version or \
       receipt["metadata"].get("TITLE_ID") != "PRME00013" or \
       not (receipt.get("commercial_game_data_included") is False or
            receipt.get("commercial_game_data_included") is True and menu_only):
        raise ValueError("Expected a verified matching game package with only its validated menu payload")
    original = safe_name(receipt["package"])
    candidates = [folder / str(original)]
    candidates += sorted(folder.glob("*.pkg"))
    for package in dict.fromkeys(candidates):
        if package.is_file() and not package.is_symlink() and \
           package.stat().st_size == receipt["bytes"] and sha(package) == receipt["package_sha256"]:
            return package, receipt
    raise ValueError("Package bytes do not match the verified build receipt")


def unpack_helper(source, destination):
    receipt = json.loads(source.with_suffix(".json").read_text(encoding="utf-8"))
    if source.name != receipt["package"] or source.stat().st_size != receipt["bytes"] or \
       sha(source) != receipt["sha256"]:
        raise ValueError("GamePrep ZIP differs from its receipt")
    with zipfile.ZipFile(source) as archive:
        rows = archive.infolist()
        names = [row.filename for row in rows]
        if len(rows) > 128 or sum(row.file_size for row in rows) > 128 * 1024 * 1024:
            raise ValueError("GamePrep ZIP exceeds expected size limits")
        if len({name.casefold() for name in names}) != len(names):
            raise ValueError("Duplicate GamePrep archive paths")
        for row in rows:
            path = safe_name(row.filename)
            if path.parts[0] != "ProjectPrime-GamePrep" or row.is_dir() or \
               stat.S_ISLNK(row.external_attr >> 16) or row.file_size > 64 * 1024 * 1024:
                raise ValueError("Unexpected GamePrep ZIP member")
        prefix = "ProjectPrime-GamePrep/"
        manifest_bytes = archive.read(prefix + "manifest.json")
        if hashlib.sha256(manifest_bytes).hexdigest() != receipt["manifest_sha256"]:
            raise ValueError("GamePrep manifest differs from its receipt")
        manifest = json.loads(manifest_bytes)
        expected = {prefix + name for name in manifest["files"]} | {prefix + "manifest.json"}
        if set(names) != expected or manifest.get("game_data_included") is not False:
            raise ValueError("Unexpected GamePrep payload")
        for name, info in manifest["files"].items():
            rel = safe_name(name)
            if name not in ("GamePrep.exe", "README.txt") and rel.parts[0] != "licenses":
                raise ValueError("Unexpected GamePrep file: " + name)
            data = archive.read(prefix + name)
            if rel.parts[0] == "licenses":
                if b"\0" in data or not any(word in rel.name.lower() for word in ("license", "copying")):
                    raise ValueError("Unexpected GamePrep license payload")
                data.decode("utf-8")
            if len(data) != info["bytes"] or hashlib.sha256(data).hexdigest() != info["sha256"]:
                raise ValueError("GamePrep payload hash mismatch: " + name)
            target = destination / str(rel)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (destination / "manifest.json").write_bytes(manifest_bytes)
    if not (destination / "GamePrep.exe").is_file():
        raise ValueError("GamePrep.exe is missing")
    return manifest, receipt


def build_bundle(version, package_dir, helper_zip, output, docs_root=ROOT):
    if not re.fullmatch(r"\d{2}\.\d{2}", version):
        raise ValueError("Version must use the PS4 format, for example 00.39")
    output = Path(output)
    if output.exists() or output.with_suffix(".json").exists():
        raise ValueError("Existing output retained; choose a fresh destination")
    package, receipt = verified_package(Path(package_dir), version)
    output.parent.mkdir(parents=True, exist_ok=True)
    display_version = player_version(version)
    with tempfile.TemporaryDirectory(prefix=".prime-download-", dir=output.parent) as temporary:
        prefix = "Metroid-Prime-PS4-" if int(version.split(".")[0]) else "Project-Prime-PS4-"
        staging = Path(temporary) / (prefix + display_version)
        (staging / "PS4").mkdir(parents=True)
        package_name = "Metroid-Prime-PS4-" + display_version + ".pkg"
        shutil.copyfile(package, staging / "PS4" / package_name)
        helper, helper_receipt = unpack_helper(Path(helper_zip), staging / "GamePrep")
        # Deliberately exclude source archives, build logs, receipts and local artwork.
        for name in ("docs/CONTROLS.md",):
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Path(docs_root) / name, target)
        licenses = Path(package_dir) / "licenses"
        if not licenses.is_dir():
            raise ValueError("Package notices/licenses directory is required")
        for path in sorted(licenses.rglob("*")):
            if path.is_symlink():
                raise ValueError("Linked license files are unsupported")
            if path.is_file():
                data = path.read_bytes()
                if b"\0" in data or path.suffix.lower() not in ("", ".txt", ".md", ".json"):
                    raise ValueError("Unexpected license payload")
                target = staging / "licenses" / path.relative_to(licenses)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        movie_note = ("The upscaled menu movies are installed with the PKG; no conversion\n"
                      "or extra movie files are needed. If you previously disabled mods,\n"
                      "enable Load mods in the port settings and restart.\n\n"
                      if receipt.get("bundled_menu_movies") else "")
        setup = (f"Metroid Prime PS4 {display_version}\n\n"
                 f"1. Install PS4/{package_name} using your homebrew package installer.\n"
                 "2. On Windows, open GamePrep/GamePrep.exe and choose your own original\n"
                 "   GameCube Metroid Prime RVZ, ISO, GCM or ZIP. You can also drag the\n"
                 "   image onto the EXE. No Python installation is needed.\n"
                 "3. After verification, copy MetroidPrime.iso from the new ProjectPrimeData\n"
                 "   folder next to your input to this exact path on the PS4 using FTP:\n"
                 "   /data/project-prime/data/MetroidPrime.iso\n"
                 "4. Launch the game from the PS4 home screen. FTP can be disconnected.\n\n"
                 "A raw ISO/GCM can also be named MetroidPrime.iso and copied directly.\n"
                 + movie_note +
                 "Use a homebrew-enabled PS4. USA GameCube revisions 0/1/2 and PAL\n"
                 "revision 0 are accepted; USA revision 0 has PS4 Slim gameplay testing.\n"
                 "Wii Trilogy and Switch images are unsupported. Renaming RVZ to ISO\n"
                 "does not convert it. ZIP input must contain one supported image.\n\n"
                 "Options pauses. Hold the touchpad click for about 0.8 seconds for port\n"
                 "settings. See docs/CONTROLS.md for layouts. Save at save stations.\n"
                 "Keep /data/project-prime/release/user/ when updating; it contains saves\n"
                 "and settings. No game image, keys or saves are bundled.\n\n"
                 f"Source and updates: {SOURCE_URL}\n")
        (staging / "README.txt").write_text(
            f"Metroid Prime PS4 {display_version}\n\n"
            "This download contains the PS4 application and Windows GamePrep helper.\n"
            "Start with INSTALLING.txt. Supply your own original GameCube game image.\n"
            f"Source and updates: {SOURCE_URL}\n", encoding="utf-8")
        (staging / "INSTALLING.txt").write_text(setup, encoding="utf-8")
        (staging / "NOTICE.txt").write_text(
            "Project Prime is an unofficial native PS4 port. Original game and component\n"
            "notices remain applicable. Game data must be supplied separately.\n"
            "Component notices: licenses/ and GamePrep/licenses/.\n"
            f"Integration source: {SOURCE_URL}\n", encoding="utf-8")
        files = {str(p.relative_to(staging)): {"bytes": p.stat().st_size, "sha256": sha(p)}
                 for p in sorted(staging.rglob("*")) if p.is_file()}
        manifest = {"format_version": 1, "version": version, "package": "PS4/" + package_name,
                    "package_sha256": receipt["package_sha256"], "game_prep_version": helper["version"],
                    "game_prep_input_sha256": helper_receipt["sha256"], "source": SOURCE_URL,
                    "game_data_included": receipt.get("commercial_game_data_included", False), "files": files}
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        checksums = {**{name: row["sha256"] for name, row in files.items()},
                     "manifest.json": sha(staging / "manifest.json")}
        (staging / "SHA256SUMS.txt").write_text(
            "".join(f"{digest}  {name}\n" for name, digest in sorted(checksums.items())), encoding="utf-8")
        compressed = Path(temporary) / "download.zip"
        with zipfile.ZipFile(compressed, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(staging.rglob("*")):
                if path.is_file():
                    archive.write(path, str(path.relative_to(staging.parent)))
        with zipfile.ZipFile(compressed) as archive:
            if archive.testzip() is not None:
                raise ValueError("Distribution ZIP CRC verification failed")
            for name, digest in checksums.items():
                if hashlib.sha256(archive.read(staging.name + "/" + name)).hexdigest() != digest:
                    raise ValueError("Distribution readback mismatch: " + name)
        with output.open("xb") as target, compressed.open("rb") as source:
            shutil.copyfileobj(source, target)
    result = {"format_version": 1, "file": output.name, "bytes": output.stat().st_size,
              "sha256": sha(output), "version": version, "package_sha256": receipt["package_sha256"],
              "game_prep_input_sha256": helper_receipt["sha256"], "zip_readback_verified": True,
              "installed": False, "launched": False,
              "game_data_included": receipt.get("commercial_game_data_included", False)}
    with output.with_suffix(".json").open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--game-prep", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build_bundle(args.version, args.package_dir, args.game_prep, args.output), indent=2))


if __name__ == "__main__":
    main()
