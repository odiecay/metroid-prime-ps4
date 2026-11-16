#!/usr/bin/env python3
"""Portable Project Prime image preparation launcher (Windows and Linux)."""
import argparse
import contextlib
import ctypes
import errno
from pathlib import Path
import subprocess
import sys
import threading
import time
import traceback

import prepare_data

VERSION = "1.0.0"
DESTINATION = "/data/project-prime/data/MetroidPrime.iso"


def readable_error(error):
    if isinstance(error, subprocess.CalledProcessError):
        if "convert" in error.cmd:
            return "The RVZ could not be converted. The backup may be incomplete or damaged."
        return "Disc verification failed. Use a complete, unmodified supported Metroid Prime backup."
    if isinstance(error, OSError):
        if error.errno == errno.ENOSPC:
            return "There is not enough free space in the output location."
        if error.errno in (errno.EACCES, errno.EPERM):
            return "Cannot read the backup or write to the output location. Choose a writable folder."
    return str(error)


def choose_file():
    """Use Windows' native Unicode file picker without a GUI dependency."""
    if sys.platform != "win32":
        raise ValueError("Choose an RVZ, ISO, GCM or ZIP on the command line.")
    from ctypes import wintypes

    class OPENFILENAMEW(ctypes.Structure):
        _fields_ = [
            ("lStructSize", wintypes.DWORD), ("hwndOwner", wintypes.HWND),
            ("hInstance", wintypes.HINSTANCE), ("lpstrFilter", wintypes.LPCWSTR),
            ("lpstrCustomFilter", wintypes.LPWSTR), ("nMaxCustFilter", wintypes.DWORD),
            ("nFilterIndex", wintypes.DWORD), ("lpstrFile", wintypes.LPWSTR),
            ("nMaxFile", wintypes.DWORD), ("lpstrFileTitle", wintypes.LPWSTR),
            ("nMaxFileTitle", wintypes.DWORD), ("lpstrInitialDir", wintypes.LPCWSTR),
            ("lpstrTitle", wintypes.LPCWSTR), ("Flags", wintypes.DWORD),
            ("nFileOffset", wintypes.WORD), ("nFileExtension", wintypes.WORD),
            ("lpstrDefExt", wintypes.LPCWSTR), ("lCustData", ctypes.c_ssize_t),
            ("lpfnHook", ctypes.c_void_p), ("lpTemplateName", wintypes.LPCWSTR),
            ("pvReserved", ctypes.c_void_p), ("dwReserved", wintypes.DWORD),
            ("FlagsEx", wintypes.DWORD),
        ]

    selected = ctypes.create_unicode_buffer(32768)
    dialog = OPENFILENAMEW()
    dialog.lStructSize = ctypes.sizeof(dialog)
    dialog.lpstrFilter = "GameCube images (*.rvz;*.iso;*.gcm;*.zip)\0*.rvz;*.iso;*.gcm;*.zip\0All files\0*.*\0\0"
    dialog.lpstrFile = ctypes.cast(selected, wintypes.LPWSTR)
    dialog.nMaxFile = len(selected)
    dialog.lpstrTitle = "Choose your Metroid Prime GameCube backup"
    dialog.Flags = 0x00001000 | 0x00000800 | 0x00080000 | 0x00000008
    dll = ctypes.WinDLL("comdlg32", use_last_error=True)
    dll.GetOpenFileNameW.argtypes = [ctypes.POINTER(OPENFILENAMEW)]
    dll.GetOpenFileNameW.restype = wintypes.BOOL
    if dll.GetOpenFileNameW(ctypes.byref(dialog)):
        return Path(selected.value)
    error = dll.CommDlgExtendedError()
    if error:
        raise OSError(f"The file picker could not open (Windows error {error}).")
    return None


def converter_path():
    name = "nodtool.exe" if sys.platform == "win32" else "nodtool"
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "bin" / name
    root = Path(__file__).resolve().parents[1]
    bundled = root / "bin" / name
    return bundled if bundled.is_file() else root / ".tools" / name


def new_output(source, requested=None):
    """Reserve a fresh directory; never reuse or remove an existing import."""
    if requested is not None:
        path = requested.expanduser().resolve()
        path.mkdir(parents=True, exist_ok=False)
        return path
    base = source.parent / "ProjectPrimeData"
    for number in range(1, 10000):
        candidate = base if number == 1 else base.with_name(f"{base.name}-{number}")
        try:
            candidate.mkdir()
            return candidate
        except FileExistsError:
            pass
    raise ValueError("Too many existing imports. Choose another output directory.")


def watch_progress(output, done, stream):
    previous = None
    last_progress = 0.0
    while not done.wait(1):
        if (output / "nod-verify.log").exists():
            stage = "Verifying the complete disc image against the checksum database..."
        elif (output / "nod-convert.log").exists():
            stage = "Converting RVZ to ISO..."
        elif (output / "MetroidPrime.partial.iso").exists():
            stage = "Copying the disc image..."
        else:
            stage = "Reading the backup..."
        now = time.monotonic()
        if stage != previous or now - last_progress >= 15:
            temporary = output / "MetroidPrime.partial.iso"
            try:
                size = f" ({temporary.stat().st_size / 1024**2:.0f} MiB written)" if "Verifying" not in stage else ""
            except OSError:
                size = ""
            print(stage + size, file=stream, flush=True)
            previous, last_progress = stage, now


def run(args):
    source = args.input.expanduser().resolve() if args.input else choose_file()
    if source is None:
        print("No file selected.")
        return 0
    if not source.is_file():
        raise ValueError("The selected backup does not exist or is not a file.")
    if source.suffix.lower() not in (".rvz", ".iso", ".gcm", ".zip"):
        raise ValueError("Choose an RVZ, ISO, GCM, or ZIP containing one of those images.")
    nodtool = (args.nodtool or converter_path()).resolve()
    if not nodtool.is_file():
        raise ValueError("The bundled converter is missing. Extract the complete GamePrep ZIP and try again.")
    output = new_output(source, args.output)
    log_path = output / "GamePrep.log"
    print(f"Backup: {source}\nOutput: {output}", flush=True)
    print("Preparing and checking your game. This may take a few minutes.\nYour original backup is kept unchanged.", flush=True)
    done = threading.Event()
    progress = threading.Thread(target=watch_progress, args=(output, done, sys.stdout), daemon=True)
    progress.start()
    original_argv = sys.argv
    try:
        with log_path.open("w", encoding="utf-8") as log:
            log.write(f"Project Prime GamePrep {VERSION}\nInput: {source}\nOutput: {output}\n")
            log.flush()
            try:
                sys.argv = ["prepare_data", str(source), "--output", str(output), "--nodtool", str(nodtool)]
                with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
                    prepare_data.main()
            except (Exception, SystemExit):
                traceback.print_exc(file=log)
                raise
    except (Exception, SystemExit) as error:
        print(f"\nCould not prepare this backup: {readable_error(error)}", flush=True)
        print(f"Details: {log_path}\nConversion/verification logs are in the same folder.", flush=True)
        return 1
    finally:
        sys.argv = original_argv
        done.set()
        progress.join(timeout=2)
    print(f"\nReady: {output / 'MetroidPrime.iso'}", flush=True)
    print(f"Copy this ISO to your PS4 as:\n  {DESTINATION}", flush=True)
    print("Then launch Project Prime. Keep import-receipt.json with your backup.", flush=True)
    return 0


def main():
    # Console output must support Unicode paths on Windows, including when redirected.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Prepare your Metroid Prime backup for PS4. Choose a file, or drag it onto GamePrep.exe.")
    parser.add_argument("input", nargs="?", type=Path, help="RVZ, ISO, GCM, or ZIP containing exactly one image")
    parser.add_argument("--output", type=Path, help="New output folder; existing folders are never overwritten")
    parser.add_argument("--nodtool", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--no-pause", action="store_true", help="Exit immediately when finished (for scripts)")
    parser.add_argument("--version", action="version", version=f"Project Prime GamePrep {VERSION}")
    args = parser.parse_args()
    print(f"Project Prime GamePrep {VERSION}\n", flush=True)
    try:
        result = run(args)
    except (Exception, SystemExit) as error:
        print(f"\nCould not prepare this backup: {readable_error(error)}", file=sys.stderr, flush=True)
        result = 1
    except KeyboardInterrupt:
        print("\nCancelled. Your original backup is unchanged.", flush=True)
        result = 130
    if sys.platform == "win32" and not args.no_pause:
        try:
            input("\nPress Enter to close...")
        except (EOFError, KeyboardInterrupt):
            pass
    return result


if __name__ == "__main__":
    raise SystemExit(main())
