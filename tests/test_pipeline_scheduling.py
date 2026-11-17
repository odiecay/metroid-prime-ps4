"""Exercise the production pipeline queue promotion and producer memo under sanitizers."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
AURORA = ROOT / "game/MetroidPrimePort/extern/aurora/lib"

class PipelineSchedulingTests(unittest.TestCase):
    def test_readiness_keeps_popped_and_completed_uninstalled_work_active(self):
        source = (AURORA / 'gfx/pipeline_cache.cpp').read_text()
        def function(signature):
            begin=source.index(signature);brace=source.index('{',begin);depth=1;end=brace+1
            while depth:
                depth+=(source[end]=='{')-(source[end]=='}');end+=1
            return source[begin:end]
        code=r'''
#include <aurora/gfx.h>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
namespace aurora::gfx {
using PipelineRef=uint64_t;
struct PendingPipeline {uint64_t hash;uint32_t firstFrameUsed;std::function<int()> create;};
struct CachedPipeline {int pipeline;uint32_t firstFrameUsed;};
std::mutex g_pipelineMutex,g_pipelineCacheWriterMutex;std::condition_variable g_pipelineQueueCv;
std::deque<PendingPipeline> g_pipelineQueue,g_backgroundPipelineQueue;
std::unordered_map<uint64_t,CachedPipeline> g_pipelines;
std::unordered_set<uint64_t> g_pendingPipelines;
std::deque<uint64_t> g_pipelinePrefetchQueue;uint32_t g_activePipelinePrefetches=0;
std::atomic_bool g_pipelineThreadEnd=false,g_dropPipelines=false;
std::atomic_uint64_t g_prepareEpoch=1,g_compileCount=0,g_compileFailures=0;
bool g_hasPipelineThread=true,g_backgroundPipelinesPaused=false;
uint32_t g_activePipelineBuilds=0;size_t g_pipelinesPerFrame=0;
constexpr size_t BuildPipelinesPerFrame=5;
std::atomic_bool compileStarted=false,finishCompile=false,mayInstall=false;
void invalidate_last_pipeline_ref(){}
int create_pipeline_timed(uint64_t,std::function<int()>&& create){
 compileStarted=true;compileStarted.notify_all();finishCompile.wait(false);
 const auto result=create();++g_compileCount;
 g_compileCount.notify_all();mayInstall.wait(false);return result;
}
void notify_pipeline_ready(bool){}
''' + function('static void apply_pipeline_drop(') + '\n' + function('static void pipeline_worker(') + '\n' + function('static void take_pipeline_prefetches_locked(') + '\n' + function('static void finish_pipeline_prefetches(') + r'''
}
''' + function('void aurora_get_pipeline_readiness(') + r'''
int main(){using namespace aurora::gfx;
 {std::lock_guard lock(g_pipelineMutex);g_pipelineQueue.push_back({1,1,[]{return 1;}});
  g_backgroundPipelineQueue.push_back({2,1,[]{return 2;}});g_pendingPipelines={1,2};}
 std::thread worker(pipeline_worker);compileStarted.wait(false);
 AuroraPipelineReadiness observed{};
 for(unsigned n=0;n<1000;++n){aurora_get_pipeline_readiness(&observed);
  assert(observed.queued==0&&observed.background==1&&observed.active==1&&observed.pending==2&&observed.completed==0);}
 finishCompile=true;finishCompile.notify_all();g_compileCount.wait(0);
 aurora_get_pipeline_readiness(&observed);
 assert(observed.completed==1&&observed.active==1&&observed.pending==2); // completed but not installed
 apply_pipeline_drop();aurora_get_pipeline_readiness(&observed);
 assert(observed.pending==0&&observed.queued==0&&observed.background==0&&observed.active==1&&observed.epoch==2);
 mayInstall=true;mayInstall.notify_all();
 do{aurora_get_pipeline_readiness(&observed);std::this_thread::yield();}while(observed.active);
 {std::lock_guard lock(g_pipelineMutex);assert(g_pipelines.contains(1));}
 assert(observed.completed==1&&observed.pending==0&&observed.active==0);
 g_dropPipelines=true;aurora_get_pipeline_readiness(&observed);assert(observed.dropPending==1);
 g_pipelineThreadEnd=true;g_pipelineQueueCv.notify_all();worker.join();g_hasPipelineThread=false;
 // A prefetch request stays represented across the writer-local batch and
 // its handoff to the compiler queue: there is no false all-idle gap.
 g_dropPipelines=false;
 {std::lock_guard lock(g_pipelineCacheWriterMutex);g_pipelinePrefetchQueue.push_back(99);}
 aurora_get_pipeline_readiness(&observed);assert(observed.prefetchQueued==1&&observed.prefetchActive==0);
 std::deque<uint64_t> local;
 {std::lock_guard lock(g_pipelineCacheWriterMutex);take_pipeline_prefetches_locked(local);}
 aurora_get_pipeline_readiness(&observed);assert(observed.prefetchQueued==0&&observed.prefetchActive==1);
 {std::lock_guard lock(g_pipelineMutex);g_backgroundPipelineQueue.push_back({99,1,[]{return 99;}});g_pendingPipelines.insert(99);}
 finish_pipeline_prefetches(local.size());
 aurora_get_pipeline_readiness(&observed);assert(observed.prefetchActive==0&&observed.background==1&&observed.pending==1);
 g_hasPipelineThread=true;g_pipelineThreadEnd=false;std::thread second(pipeline_worker);
 do{aurora_get_pipeline_readiness(&observed);std::this_thread::yield();}while(observed.background||observed.active||observed.pending);
 {std::lock_guard lock(g_pipelineMutex);assert(g_pipelines.contains(99));}
 g_pipelineThreadEnd=true;g_pipelineQueueCv.notify_all();second.join();g_hasPipelineThread=false;
 aurora_get_pipeline_readiness(nullptr);
 std::puts("actual_pipeline_readiness active_after_pop=1 completed_not_installed=1 drop_keeps_active=1 atomic_install=1 prefetch_handoff=1");
}
'''
        out=ROOT/'build/pipeline-readiness-host';out.mkdir(exist_ok=True)
        unit=out/'actual.cpp';unit.write_text(code)
        for sanitizer in (False,True):
            program=out/('sanitized' if sanitizer else 'normal')
            command=['g++-14','-std=c++20','-O2','-pthread','-I'+str(AURORA.parent/'include'),str(unit),'-o',str(program)]
            if sanitizer:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            subprocess.run(command,check=True)
            result=subprocess.run([program],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('completed_not_installed=1',result.stdout)

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
