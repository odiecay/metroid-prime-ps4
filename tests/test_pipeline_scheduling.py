"""Exercise the production pipeline queue promotion and producer memo under sanitizers."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
AURORA = ROOT / "game/MetroidPrimePort/extern/aurora/lib"

class PipelineSchedulingTests(unittest.TestCase):
    def test_blocking_promotion_and_context_cache(self):
        source = (AURORA / "gfx/pipeline_cache.cpp").read_text()
        queue_functions = source.split("template <typename Queue>\nstatic auto find_pending_pipeline", 1)[1].split("\nstatic std::optional<PendingPipeline> take_pending_pipeline", 1)[0]
        code = r'''
#include "gfx/pipeline_prepare_memo.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <random>
#include <vector>
using PipelineRef = uint64_t;
enum class PipelinePriority {Background, Normal, Blocking};
struct PendingPipeline {PipelineRef hash;std::unique_ptr<unsigned> create;};
std::deque<PendingPipeline> g_pipelineQueue, g_backgroundPipelineQueue;
std::atomic_uint64_t g_lateMisses = 0;
namespace trace {enum Lane {Render};void count(const char*,uint64_t,Lane){}}
template <typename Queue>
static auto find_pending_pipeline''' + queue_functions + r'''
std::vector<uint64_t> hashes(const auto& q) {
 std::vector<uint64_t> out;for(const auto& p:q)out.push_back(p.hash);return out;
}
void fill(std::deque<PendingPipeline>& q,unsigned start,unsigned count) {
 q.clear();for(unsigned i=0;i<count;++i)q.push_back({start+i,std::make_unique<unsigned>(start+i)});
}
int main() {
 // The old implementation left 64 behind all 63 earlier normal requests.
 fill(g_pipelineQueue,1,64);
 auto* urgent=touch_pending_pipeline(64,PipelinePriority::Blocking);
 assert(urgent==&g_pipelineQueue.front() && urgent->hash==64 && *urgent->create==64);
 std::vector<uint64_t> expected{64};for(unsigned i=1;i<64;++i)expected.push_back(i);
 assert(hashes(g_pipelineQueue)==expected);
 // Promotion of the front is idempotent and never loses its callback ownership.
 auto* callback=urgent->create.get();
 urgent=touch_pending_pipeline(64,PipelinePriority::Blocking);
 assert(urgent->create.get()==callback && hashes(g_pipelineQueue)==expected);
 touch_pending_pipeline(32,PipelinePriority::Normal);
 assert(hashes(g_pipelineQueue)==expected); // normal lookups preserve FIFO order
 fill(g_backgroundPipelineQueue,100,4);
 urgent=touch_pending_pipeline(102,PipelinePriority::Blocking);
 assert(urgent->hash==102 && *urgent->create==102);
 assert(hashes(g_backgroundPipelineQueue)==std::vector<uint64_t>({100,101,103}));
 assert(touch_pending_pipeline(999,PipelinePriority::Blocking)==nullptr);
 // Exercise front/middle/back promotions with move-only callbacks in shuffled order.
 std::mt19937 rng(0x5052494d);
 for(unsigned run=0;run<100;++run) {
  fill(g_pipelineQueue,1,64);
  std::vector<uint64_t> order;for(unsigned i=1;i<=64;++i)order.push_back(i);
  for(unsigned step=0;step<1000;++step) {
   auto id=1+rng()%64;auto i=std::find(order.begin(),order.end(),id);
   order.erase(i);order.insert(order.begin(),id);
   auto* got=touch_pending_pipeline(id,PipelinePriority::Blocking);
   assert(got==&g_pipelineQueue.front() && *got->create==id);
   assert(hashes(g_pipelineQueue)==order);
  }
 }
 aurora::gfx::PipelinePrepareMemo memo;
 assert(!memo.contains(0,0,0));
 memo.remember(0,0,1);assert(memo.contains(0,0,1));
 assert(!memo.contains(0,1,1)); // same shader in another room still records a use
 assert(!memo.contains(0,0,2)); // a recreated device/drop still prepares it
 memo.remember(512,0,1); // deliberately collides with shader zero
 assert(memo.contains(512,0,1) && !memo.contains(0,0,1));
 for(unsigned i=0;i<100000;++i) {
  auto hash=uint64_t(rng())<<32|rng();auto context=uint64_t(rng());
  memo.remember(hash,context,17);
  assert(memo.contains(hash,context,17) && !memo.contains(hash,context,18));
 }
 puts("Production queue: 100000 stable urgent promotions; move-only callbacks and bounded room/device memo preserved");
}
'''
        out = ROOT / "build/pipeline-scheduling-host"
        out.mkdir(exist_ok=True)
        unit = out / "actual_pipeline_scheduling.cpp"
        unit.write_text(code)
        program = out / "run"
        subprocess.run(["g++-14", "-std=c++20", "-O2", "-fsanitize=address,undefined",
                        "-I" + str(AURORA), str(unit), "-o", str(program)], check=True)
        result = subprocess.run([program], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("100000 stable urgent promotions", result.stdout)

if __name__ == "__main__":
    unittest.main()
