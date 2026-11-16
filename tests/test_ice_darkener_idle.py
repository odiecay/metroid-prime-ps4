"""Run the actual old/new game methods against identical room/timer fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "game/MetroidPrimePort"


def function(text, name):
    start = text.index(name)
    begin = text.index("{", start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


class IceDarkenerIdleTests(unittest.TestCase):
    def test_actual_methods_preserve_timer_delivery_and_remove_idle_allocations(self):
        out = ROOT / "build/ice-darkener-idle"
        out.mkdir(parents=True, exist_ok=True)
        name = "void CStateManager::PortUpdateIceDarkeners(float dt)"
        current = function((GAME / "src/MetroidPrime/CStateManager.cpp").read_text(), name)
        baseline = subprocess.check_output(
            ["git", "-C", str(GAME), "show", "HEAD:src/MetroidPrime/CStateManager.cpp"], text=True)
        reference = function(baseline, name).replace("::PortUpdateIceDarkeners", "::ReferenceUpdateIceDarkeners")
        metadata = function((GAME / "platform/port_room_env.cpp").read_text(), "bool HasIceDarkeners()")
        for filename, source in [("ice_current_method.inc", current), ("ice_reference_method.inc", reference),
                                 ("ice_metadata_query.inc", metadata)]:
            (out / filename).write_text(source)
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        flags = ["-std=c++20", "-O2", "-g", "-I", str(out)]
        if os.environ.get("PRIME_ICE_SANITIZE") == "1":
            compiler = shutil.which("g++")
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command = [compiler, *flags, str(ROOT / "tests/ice_darkener_idle.cpp"), "-o", str(out / "run")]
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        env = dict(os.environ)
        env.pop("MP_ICE_DARKENERS", None)
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        run = subprocess.run([out / "run"], env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("old_allocations=100000 new_allocations=0", run.stdout)
        self.assertIn("lifecycle_equivalent=1", run.stdout)
        print(run.stdout, end="")


if __name__ == "__main__":
    unittest.main()
