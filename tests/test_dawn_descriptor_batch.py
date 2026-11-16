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
        (out / "actual_descriptor_batch.inc").write_text(source[start:end])
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

if __name__ == "__main__":
    unittest.main()
