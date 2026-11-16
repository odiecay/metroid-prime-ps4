"""Exercise real Dawn immediate storage and the actual Vulkan tracker override."""
import json
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/pc-asset-importer"


class DawnImmediateInheritanceTests(unittest.TestCase):
    def test_equal_masks_imply_identical_actual_vulkan_push_ranges(self):
        source = ROOT / "external/dawn/src/dawn/native/vulkan"
        layout = (source / "PipelineLayoutVk.cpp").read_text()
        self.assertIn("pushConstantRange.stageFlags = kImmediateShaderStages;", layout)
        self.assertIn("pushConstantRange.offset = 0;", layout)
        self.assertIn("pushConstantRange.size = specialization.pushConstantBytes;", layout)
        self.assertIn("createInfo.pushConstantRangeCount = 1;", layout)
        for name in ("RenderPipelineVk.cpp", "ComputePipelineVk.cpp"):
            self.assertIn(".pushConstantBytes = ToPushConstantBytes(mImmediateMask)",
                          (source / name).read_text())
        size = (source / "UtilsVulkan.cpp").read_text().split(
            "uint32_t ToPushConstantBytes(const ImmediateMask& immediates) {", 1)[1].split("}", 1)[0]
        self.assertIn("immediates.count()) * kImmediateElementByteSize", size)

    @unittest.skipUnless((BUILD / "compile_commands.json").exists(),
                         "Configure and build the existing PC importer/Dawn Null test dependencies first")
    def test_actual_dirty_words_and_push_ranges_remain_identical(self):
        source = (ROOT / "external/dawn/src/dawn/native/vulkan/CommandBufferVk.cpp").read_text()
        start = source.index("template <typename T>\nclass ImmediateTracker")
        end = source.index("// Records the necessary barriers", start)
        output = ROOT / "build/dawn-immediate-inheritance-host"
        output.mkdir(exist_ok=True)
        (output / "actual_immediate_tracker.inc").write_text(source[start:end])
        commands = json.loads((BUILD / "compile_commands.json").read_text())
        recipe = next(c for c in commands if c["file"].endswith("/PassResourceUsageTracker.cpp"))
        compile_args = shlex.split(recipe["command"])
        obj = output / "test.o"
        compile_args[compile_args.index("-o") + 1] = str(obj)
        compile_args[compile_args.index("-c") + 1] = str(ROOT / "tests/dawn_immediate_inheritance.cpp")
        compile_args += ["-UNDEBUG", "-I" + str(output)]
        subprocess.run(compile_args, cwd=recipe["directory"], check=True, capture_output=True, text=True)

        # Reuse the real configured target's current library order and ABI.
        command = subprocess.check_output(["ninja", "-C", str(BUILD), "-t", "commands",
                                           "gfx_recording_tests"], text=True).splitlines()[-1]
        tokens = shlex.split(command)
        self.assertEqual(tokens[:2], [":", "&&"])
        self.assertEqual(tokens[-2:], ["&&", ":"])
        args = tokens[2:-2]
        args = [arg for arg in args if not ("gfx_recording_tests.dir/" in arg and arg.endswith(".o"))]
        dependency_flag = next((i for i, arg in enumerate(args) if arg.startswith("--dependency-file=")), None)
        if dependency_flag is not None:
            self.assertEqual(args[dependency_flag - 1], "-Xlinker")
            del args[dependency_flag - 1:dependency_flag + 1]
        program = output / "run"
        args[args.index("-o") + 1] = str(program)
        args.insert(args.index("-o"), str(obj))
        subprocess.run(args, cwd=BUILD, check=True, capture_output=True, text=True)
        result = subprocess.run([program], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("immediate_draws=100000", result.stdout)
        self.assertIn("exact_values_and_ranges=1 fresh_passes=1000", result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
