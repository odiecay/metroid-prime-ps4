"""Exercise the production GUI camera and front-end movie composition."""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "game/MetroidPrimePort"


def function(source, name):
    start = source.index(name)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


class FrontEndLayoutTests(unittest.TestCase):
    def test_actual_camera_preserves_native_menu_layout(self):
        out = ROOT / "build/frontend-hd/host"
        out.mkdir(parents=True, exist_ok=True)
        source = (GAME / "src/GuiSys/CGuiCamera.cpp").read_text()
        (out / "frontend_camera_draw.inc").write_text(
            function(source, "void CGuiCamera::Draw("))
        flags = ["-std=c++20", "-O2", "-g", "-I", str(out),
                 "-I", str(GAME / "platform/include")]
        compiler = shutil.which("clang++") or shutil.which("g++")
        if os.environ.get("PRIME_FRONTEND_SANITIZE") == "1":
            compiler = shutil.which("g++")
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        built = subprocess.run([compiler, *flags, str(ROOT / "tests/frontend_layout.cpp"),
                                "-o", str(out / "run")], capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([out / "run"], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("all_corners_visible=1 movie_scale_uniform=1", run.stdout)
        print(run.stdout, end="")

    def test_video_quality_preserves_yuv_and_texture_ownership(self):
        out = ROOT / "build/frontend-hd/host-video"
        out.mkdir(parents=True, exist_ok=True)
        source = (GAME / "src/Kyoto/Graphics/CCubeMoviePlayer.cpp").read_text()
        baseline = subprocess.check_output(
            ["git", "-C", str(GAME), "show", "HEAD:src/Kyoto/Graphics/CCubeMoviePlayer.cpp"], text=True)
        self.assertEqual(function(source, "static void MyTHPGXYuv2RgbSetup("),
                         function(baseline, "static void MyTHPGXYuv2RgbSetup("))
        (out / "frontend_video_filter.inc").write_text(
            function(source, "static void MyTHPYuv2RgbTextureSetup("))
        compiler = shutil.which("clang++") or shutil.which("g++")
        flags = ["-std=c++20", "-O2", "-g", "-I", str(out)]
        if os.environ.get("PRIME_FRONTEND_SANITIZE") == "1":
            compiler = shutil.which("g++")
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        built = subprocess.run([compiler, *flags, str(ROOT / "tests/frontend_video_filter.cpp"),
                                "-o", str(out / "run")], capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([out / "run"], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("texture_ownership_unchanged=1", run.stdout)
        print(run.stdout, end="")

    def test_frontend_changes_do_not_replace_navigation(self):
        current = (GAME / "src/MetroidPrime/CFrontEndUI.cpp").read_text()
        baseline = subprocess.check_output(
            ["git", "-C", str(GAME), "show", "HEAD:src/MetroidPrime/CFrontEndUI.cpp"], text=True)
        for name in ["void CFrontEndUI::ProcessUserInput(",
                     "CFrontEndUI::SFrontEndFrame::ProcessUserInput(",
                     "CFrontEndUI::SNewFileSelectFrame::ProcessUserInput("]:
            self.assertEqual(function(current, name), function(baseline, name), name)


if __name__ == "__main__":
    unittest.main()
