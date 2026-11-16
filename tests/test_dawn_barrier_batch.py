"""Compare actual Vulkan image barrier batches and their encoding allocations."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DawnBarrierBatchTests(unittest.TestCase):
    def test_barrier_bytes_order_and_flags_are_unchanged_without_copies(self):
        source = (ROOT / "external/dawn/src/dawn/native/vulkan/CommandBufferVk.cpp").read_text()
        start = source.index("    // Visit the two batches directly.")
        end = source.index("    recordingContext->EmitBufferBarrierIfNecessary", start)
        output = ROOT / "build/dawn-barrier-batch-host"
        output.mkdir(exist_ok=True)
        (output / "actual_barrier_batch.inc").write_text(source[start:end])
        program = output / "run"
        subprocess.run(["g++-14", "-std=c++20", "-O2", "-fsanitize=address,undefined",
                        "-I" + str(output), str(ROOT / "tests/dawn_barrier_batch.cpp"),
                        "-o", str(program)], check=True)
        result = subprocess.run([program], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("barrier_cases=10000", result.stdout)
        self.assertIn("current_vector_allocations=0", result.stdout)
        print(result.stdout.strip())

    def test_native_scopes_remain_pass_granularity(self):
        source = (ROOT / "external/dawn/src/dawn/native/vulkan/CommandBufferVk.cpp").read_text()
        self.assertEqual(source.count("PrimeDawnScope profileResources(3);"), 1)
        self.assertEqual(source.count("PrimeDawnScope profileReplay(4);"), 1)
        self.assertEqual(source.count("PrimeDawnScope profilePassSetup(5);"), 1)
        replay = source.split("MaybeError CommandBuffer::RecordRenderPass(", 1)[1]
        self.assertLess(replay.index("profilePassSetup"), replay.index("RenderPassState state"))
        self.assertLess(replay.index("profileReplay"), replay.index("auto EncodeRenderBundleCommand"))


if __name__ == "__main__":
    unittest.main()
