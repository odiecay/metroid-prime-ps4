#!/usr/bin/env python3
"""Build the portable Windows x64 GamePrep ZIP on Linux using isolated Wine.

Requires Python 3.11+, wine and xvfb-run. All downloaded inputs have pinned SHA256s.
This script never touches a console or includes game data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.0.0"


def sha(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def download(item, directory):
    target = directory / item["filename"]
    if target.is_file() and sha(target) == item["sha256"]:
        return target
    print(f"Downloading {item['filename']}", flush=True)
    temporary = target.with_suffix(target.suffix + ".part")
    with urllib.request.urlopen(item["url"], timeout=120) as source, temporary.open("wb") as output:
        shutil.copyfileobj(source, output)
    if sha(temporary) != item["sha256"]:
        raise ValueError(f"SHA256 mismatch: {item['filename']}")
    temporary.replace(target)
    return target


def windows_path(path):
    return "Z:" + str(Path(path).resolve()).replace("/", "\\")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, default=ROOT / "build/game-prep-wine")
    parser.add_argument("--output", type=Path, default=ROOT / f"artifacts/game-prep-v{VERSION}")
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("This reproducible builder currently runs on Linux with Wine.")
    for command in ("wine", "xvfb-run"):
        if shutil.which(command) is None:
            parser.error(f"Missing build requirement: {command}")
    work, output = args.work_dir.resolve(), args.output.resolve()
    downloads = work / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    final = output / f"ProjectPrime-GamePrep-Windows-x64-{VERSION}.zip"
    if final.exists():
        parser.error(f"Existing package retained: {final}. Choose a fresh --output directory.")
    inputs_path = ROOT / "tools/game_prep_build_inputs.json"
    inputs = json.loads(inputs_path.read_text(encoding="utf-8"))
    for item in [inputs["python"], inputs["converter"], *inputs["wheels"], *inputs["licenses"]]:
        download(item, downloads)
    python_dir = work / "python"
    python_dir.mkdir(exist_ok=True)
    with zipfile.ZipFile(downloads / inputs["python"]["filename"]) as archive:
        archive.extractall(python_dir)
    packages = python_dir / "Lib/site-packages"
    packages.mkdir(parents=True, exist_ok=True)
    for item in inputs["wheels"]:
        with zipfile.ZipFile(downloads / item["filename"]) as archive:
            archive.extractall(packages)
    (python_dir / "python312._pth").write_text("python312.zip\n.\nLib/site-packages\nimport site\n", encoding="ascii")
    environment = dict(os.environ, WINEPREFIX=str(work / "prefix"), WINEARCH="win64", WINEDEBUG="-all")
    # Keep Wine output attached: redirecting embedded Python's initial console
    # handles can cause WinError 6 before Python initializes on some Wine builds.
    command = [
        "xvfb-run", "-a", "wine", str(python_dir / "python.exe"), "-m", "PyInstaller",
        "--clean", "--noconfirm", "--onefile", "--console", "--name", "GamePrep",
        "--distpath", windows_path(work / "dist"),
        "--workpath", windows_path(work / "pyinstaller"),
        "--specpath", windows_path(work), "--paths", windows_path(ROOT / "tools"),
        "--add-binary", windows_path(downloads / "nodtool.exe") + ";bin",
        windows_path(ROOT / "tools/game_prep_gui.py"),
    ]
    subprocess.run(command, env=environment, check=True, cwd=work)
    executable = work / "dist/GamePrep.exe"
    if not executable.is_file():
        raise ValueError("PyInstaller did not produce GamePrep.exe")
    for option in ("--version", "--help"):
        subprocess.run(["xvfb-run", "-a", "wine", str(executable), option], env=environment, check=True, timeout=120)
    stage = work / "package/ProjectPrime-GamePrep"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    shutil.copyfile(executable, stage / "GamePrep.exe")
    licenses = stage / "licenses"
    licenses.mkdir()
    shutil.copyfile(python_dir / "LICENSE.txt", licenses / "Python-LICENSE.txt")
    for item in inputs["licenses"]:
        shutil.copyfile(downloads / item["filename"], licenses / ("nod-" + item["filename"] + ".txt"))
    for info in sorted(packages.glob("*.dist-info")):
        for path in sorted(info.rglob("*")):
            if path.is_file() and any(word in path.name.lower() for word in ("license", "copying")):
                relative = path.relative_to(info)
                name = info.name + "-" + str(relative).replace("/", "-")
                shutil.copyfile(path, licenses / name)
    (stage / "README.txt").write_text(
        "Project Prime GamePrep 1.0.0 - Windows x64\n\n"
        "Double-click GamePrep.exe and choose your Metroid Prime RVZ, ISO, GCM\n"
        "or ZIP backup. You can also drag the backup onto GamePrep.exe.\n"
        "No Python installation is needed. Keep the window open while it works.\n\n"
        "The verified MetroidPrime.iso appears in a new ProjectPrimeData folder\n"
        "next to your backup. Your original file is kept unchanged.\n"
        "Copy the prepared ISO to your PS4 as:\n"
        "  /data/project-prime/data/MetroidPrime.iso\n"
        "Install the Project Prime PS4 PKG, then launch the game.\n\n"
        "Supported: GameCube Metroid Prime USA revisions 0, 1 and 2; Europe\n"
        "revision 0. ZIPs must contain exactly one RVZ, ISO or GCM. Switch\n"
        "Remastered, Trilogy and other game releases are not supported here.\n"
        "Allow up to 3 GB of free space for conversion and verification.\n\n"
        "If preparation fails, GamePrep shows the error and log folder.\n"
        "The tool includes nodtool v2.0.0-alpha.12 (MIT OR Apache-2.0),\n"
        "CPython 3.12.8 and the PyInstaller 6.16.0 bootloader. Notices are in\n"
        "licenses/. No game data or keys are included.\n\n"
        "Converter source: https://github.com/encounter/nod/tree/v2.0.0-alpha.12\n"
        "Tool source: https://github.com/odiecay/metroid-prime-ps4\n",
        encoding="utf-8",
    )
    manifest = {
        "format_version": 1, "name": "Project Prime GamePrep", "version": VERSION,
        "platform": "Windows x64", "python_required_by_user": False,
        "converter_source": "https://github.com/encounter/nod/tree/v2.0.0-alpha.12",
        "build_inputs": inputs,
        "source_sha256": {str(p.relative_to(ROOT)): sha(p) for p in (
            ROOT / "tools/prepare_data.py", ROOT / "tools/game_prep_gui.py",
            ROOT / "tools/build_game_prep.py", inputs_path)},
        "files": {str(p.relative_to(stage)): {"bytes": p.stat().st_size, "sha256": sha(p)}
                  for p in sorted(stage.rglob("*")) if p.is_file()},
        "test_scope": "Wine --version and --help; see separate test receipt for image conversion",
        "game_data_included": False,
    }
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    with zipfile.ZipFile(final, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(stage.parent))
    with zipfile.ZipFile(final) as archive:
        if archive.testzip() is not None:
            raise ValueError("Finished ZIP failed its CRC check")
    receipt = {"package": final.name, "bytes": final.stat().st_size, "sha256": sha(final),
               "manifest_sha256": sha(stage / "manifest.json")}
    final.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == "__main__":
    main()
