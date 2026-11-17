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
        for native in (False, True):
            binary = out / ("native" if native else "run")
            native_flags = ["-D__ORBIS__"] if native else []
            built = subprocess.run([compiler, *flags, *native_flags,
                                    str(ROOT / "tests/frontend_layout.cpp"), "-o", str(binary)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([binary], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("all_corners_visible=1 movie_scale_uniform=1", run.stdout)
            print(run.stdout, end="")

    def test_video_quality_preserves_yuv_and_texture_ownership(self):
        out = ROOT / "build/frontend-hd/host-video"
        out.mkdir(parents=True, exist_ok=True)
        source = (GAME / "src/Kyoto/Graphics/CCubeMoviePlayer.cpp").read_text()
        baseline = subprocess.check_output(
            ["git", "-C", str(GAME), "show", "HEAD:src/Kyoto/Graphics/CCubeMoviePlayer.cpp"], text=True)
        # The native progressive correction precedes the original setup. All
        # YUV coefficients, texture inputs and the other platforms' field-mask
        # path remain exactly as authored.
        self.assertEqual(function(source, "static void MyTHPGXYuv2RgbSetup(").split('  GXVtxDescList attr[3]', 1)[1],
                         function(baseline, "static void MyTHPGXYuv2RgbSetup(").split('  GXVtxDescList attr[3]', 1)[1])
        (out / "frontend_video_filter.inc").write_text(
            function(source, "static void MyTHPYuv2RgbTextureSetup("))
        (out / "linear_menu_selector.inc").write_text(function(source, "bool CMoviePlayer::PortUsesLinearMenuPlanes("))
        compiler = shutil.which("clang++") or shutil.which("g++")
        flags = ["-std=c++20", "-O2", "-g", "-I", str(out)]
        if os.environ.get("PRIME_FRONTEND_SANITIZE") == "1":
            compiler = shutil.which("g++")
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        for native in (False, True):
            binary = out / ("native" if native else "run")
            built = subprocess.run([compiler, *flags, *(["-D__ORBIS__"] if native else []),
                                    str(ROOT / "tests/frontend_video_filter.cpp"), "-o", str(binary)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([binary], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("texture_ownership_unchanged=1", run.stdout)
            print(run.stdout, end="")

    def test_native_movie_conversion_omits_only_output_field_mask(self):
        out = ROOT / 'build/frontend-hd/host-progressive'
        out.mkdir(parents=True, exist_ok=True)
        source = (GAME / 'src/Kyoto/Graphics/CCubeMoviePlayer.cpp').read_text()
        baseline = subprocess.check_output(
            ['git', '-C', str(GAME), 'show', 'HEAD:src/Kyoto/Graphics/CCubeMoviePlayer.cpp'], text=True)
        original = function(baseline, 'static void MyTHPGXYuv2RgbSetup(')
        current = function(source, 'static void MyTHPGXYuv2RgbSetup(')
        pattern = source[source.index('const unsigned char skInterlacePattern[32]'):]
        pattern = pattern[:pattern.index(';')+1]
        (out / 'movie_conversion.inc').write_text(pattern + '\n' +
            original.replace('MyTHPGXYuv2RgbSetup', 'OriginalSetup') + current +
            function(source, 'static void MyTHPGXRestore('))
        for native, sanitized in ((False, False), (True, False), (True, True)):
            binary = out / ('sanitized' if sanitized else 'native' if native else 'other')
            command = ['g++-14', '-std=c++20', '-O2', '-DTARGET_PC', '-I'+str(out),
                       '-I'+str(GAME/'extern/aurora/include'),
                       str(ROOT/'tests/frontend_movie_progressive.cpp'), '-o', str(binary)]
            if native:
                command += ['-D__ORBIS__']
            if sanitized:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('fields=2 requested_modes=2 yuv_unchanged=1 alpha_restored=1', result.stdout)
            print(result.stdout.strip())

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
