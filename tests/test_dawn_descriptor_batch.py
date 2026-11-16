"""Exercise the real Dawn Vulkan descriptor emission block with captured API calls."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DawnDescriptorBatchTests(unittest.TestCase):
    def test_sets_offsets_and_layout_effects_are_preserved(self):
        source = (ROOT / "external/dawn/src/dawn/native/vulkan/CommandBufferVk.cpp").read_text()
        start = source.index("        // Consecutive dirty sets share this pipeline layout.")
        end = source.index("        // Update PipelineLayout", start)
        out = ROOT / "build/dawn-descriptor-batch-host"
        out.mkdir(exist_ok=True)
        (out / "actual_descriptor_batch.inc").write_text("#define PRIME_DAWN_VK_CALL(category, function, ...) function(__VA_ARGS__)\n"+source[start:end])
        program = out / "run"
        subprocess.run(["g++-14", "-std=c++20", "-O2", "-fsanitize=address,undefined",
                        "-I" + str(out), str(ROOT / "tests/dawn_descriptor_batch.cpp"),
                        "-o", str(program)], check=True)
        result = subprocess.run([program], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("cases=103888", result.stdout)
        self.assertIn("gx_calls=2->1", result.stdout)
        print(result.stdout.strip())


    def test_tracker_emits_once_and_cannot_be_copied(self):
        source = (ROOT / "external/dawn/src/dawn/native/vulkan/CommandBufferVk.cpp").read_text()
        tracker = source.split("class DescriptorSetTracker : public BindGroupTrackerBase<true> {", 1)[1].split("\nclass RenderImmediatesTracker", 1)[0]
        prefix = tracker.split("  public:", 1)[1].split("    bool AreLayoutsCompatible()", 1)[0]
        fields = tracker.rsplit("  private:", 1)[1].rsplit("};", 1)[0]
        unit_source = r'''
#define __ORBIS__ 1
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
extern "C" void prime_ps4_descriptor_bind_counts(uint64_t,uint64_t) __attribute__((weak));
std::vector<std::pair<uint64_t,uint64_t>> observed;
#ifndef NO_OBSERVER
extern "C" void prime_ps4_descriptor_bind_counts(uint64_t calls,uint64_t sets){observed.emplace_back(calls,sets);}
#endif
template<bool>struct BindGroupTrackerBase {virtual ~BindGroupTrackerBase()=default;};
class DescriptorSetTracker : public BindGroupTrackerBase<true> {
public:
''' + prefix + "\nprivate:\n" + fields + r'''
};
static_assert(!std::is_copy_constructible_v<DescriptorSetTracker>);
static_assert(!std::is_copy_assignable_v<DescriptorSetTracker>);
static_assert(!std::is_move_constructible_v<DescriptorSetTracker>);
static_assert(!std::is_move_assignable_v<DescriptorSetTracker>);
void early_return(){DescriptorSetTracker t;t.NoteDescriptorBind(3);return;}
int main() {
 {DescriptorSetTracker empty;}
 assert(observed.empty());
 {
  DescriptorSetTracker t;t.NoteDescriptorBind(1);t.NoteDescriptorBind(2);t.NoteDescriptorBind(1);
  assert(observed.empty()); // no callback on individual bind/draw
 }
#ifndef NO_OBSERVER
 assert(observed.size()==1 && observed[0].first==3 && observed[0].second==4);
#endif
 early_return();
#ifndef NO_OBSERVER
 assert(observed.size()==2 && observed[1].first==1 && observed[1].second==3);
 observed.clear();
#endif
 for(unsigned i=0;i<1000;++i) {
  DescriptorSetTracker t;
  for(unsigned j=0;j<100;++j)t.NoteDescriptorBind(2);
 }
#ifndef NO_OBSERVER
 assert(observed.size()==1000);
 for(auto [calls,sets]:observed)assert(calls==100 && sets==200);
#else
 assert(observed.empty()); // the optional observer genuinely need not be linked
#endif
}
'''
        out = ROOT / "build/dawn-descriptor-batch-host"
        out.mkdir(exist_ok=True)
        unit = out / "actual_tracker_counts.cpp"
        unit.write_text(unit_source)
        for absent in (False, True):
            program = out / ("tracker_without_observer" if absent else "tracker_with_observer")
            args = ["g++-14", "-std=c++20", "-O2", "-fsanitize=address,undefined"]
            if absent:
                args += ["-DNO_OBSERVER"]
            subprocess.run(args + [str(unit), "-o", str(program)], check=True)
            result = subprocess.run([program], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_same_pipeline_compatibility_preserves_layout_and_immediate_checks(self):
        import json
        def method(text):
            start = text.index("    bool AreLayoutsCompatible() override {")
            end = text.index("\n    const BindGroupBase*", start)
            return text[start:end]
        current = (ROOT / "external/dawn/src/dawn/native/vulkan/CommandBufferVk.cpp").read_text()
        lock = json.loads((ROOT / "upstream.lock.json").read_text())
        pin = next(c["commit"] for c in lock["components"] if c["path"] == "external/dawn")
        reference = subprocess.check_output([
            "git", "-C", str(ROOT / "external/dawn"), "show",
            pin + ":src/dawn/native/vulkan/CommandBufferVk.cpp"], text=True)
        fixture = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
struct Pipeline {
    const int* layout;
    inline static uint32_t getterCalls = 0;
    const int* GetLayout() { ++getterCalls; return layout; }
};
struct Base { virtual bool AreLayoutsCompatible() = 0; };
struct State : Base {
    Pipeline* mLastAppliedPipeline = nullptr;
    Pipeline* mPipeline = nullptr;
    uint32_t mLastAppliedImmediateSize = 0;
    uint32_t mImmediateSize = 0;
};
struct Reference : State {
''' + method(reference) + "\n};\nstruct Current : State {\n" + method(current) + r'''
};
int main() {
    std::array<int, 4> layouts{};
    std::array<Pipeline, 8> pipelines;
    for (unsigned i = 0; i < pipelines.size(); ++i) pipelines[i].layout = &layouts[i % 4];
    uint32_t cases = 0;
    for (unsigned last = 0; last <= pipelines.size(); ++last)
    for (auto& current : pipelines)
    for (uint32_t beforeSize = 0; beforeSize < 17; ++beforeSize)
    for (uint32_t afterSize = 0; afterSize < 17; ++afterSize) {
        Reference a; Current b;
        a.mLastAppliedPipeline = b.mLastAppliedPipeline = last == pipelines.size() ? nullptr : &pipelines[last];
        a.mPipeline = b.mPipeline = &current;
        a.mLastAppliedImmediateSize = b.mLastAppliedImmediateSize = beforeSize;
        a.mImmediateSize = b.mImmediateSize = afterSize;
        const bool expected = a.AreLayoutsCompatible();
        Pipeline::getterCalls = 0;
        assert(b.AreLayoutsCompatible() == expected);
        const uint32_t calls = !b.mLastAppliedPipeline || b.mLastAppliedPipeline == b.mPipeline ? 0 : 2;
        assert(Pipeline::getterCalls == calls);
        ++cases;
    }
    assert(cases == 20808);
    std::printf("descriptor_compatibility_cases=%u exact_results=1 same_object_getters=0 changed_object_getters=2\n", cases);
}
'''
        out = ROOT / "build/dawn-descriptor-batch-host"
        out.mkdir(exist_ok=True)
        unit = out / "actual_layout_compatibility.cpp"
        unit.write_text(fixture)
        program = out / "layout_compatibility"
        subprocess.run(["g++-14", "-std=c++20", "-O2", "-fsanitize=address,undefined",
                        str(unit), "-o", str(program)], check=True)
        result = subprocess.run([program], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("descriptor_compatibility_cases=20808 exact_results=1", result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
