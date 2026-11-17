"""Exercise actual producer cache invalidation, including every 64-bit pipeline ref."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(os.environ.get('PROJECT_PRIME_ROOT', Path(__file__).resolve().parents[1]))
SOURCE = ROOT / 'game/MetroidPrimePort/extern/aurora/lib/gfx/pipeline_cache.cpp'


def function(source, signature):
    start = source.index(signature)
    begin = source.index('{', start)
    depth, end = 1, begin + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class PipelineTouchLifecycleTests(unittest.TestCase):
    def test_drop_context_eviction_shutdown_and_ref_boundaries(self):
        source = Path(os.environ.get('PIPELINE_TOUCH_SOURCE', SOURCE)).read_text()
        bodies = '\n'.join(function(source, signature) for signature in (
            'static void invalidate_last_pipeline_ref(', 'static bool take_pipeline_drop_request(',
            'static void apply_pipeline_drop(',
            'bool touch_pipeline(', 'void shutdown_pipeline_cache(',
            'static uint32_t evict_idle_pipelines(', 'uint64_t aurora_pipeline_set_context(')
        )
        # Initialization is exercised by the full-cache fixture as well. Keep
        # this fast regression tied to its production invalidation entry point.
        self.assertIn('invalidate_last_pipeline_ref();', function(source, 'void initialize_pipeline_cache('))
        unit = r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace aurora::gfx {
using PipelineRef=uint64_t;
constexpr uint64_t AURORA_PIPELINE_CONTEXT_FRONTEND=1;
std::atomic_uint32_t g_pipelineFrame=0;
std::atomic_uint64_t g_prepareEpoch=1,g_pipelineContext=AURORA_PIPELINE_CONTEXT_FRONTEND;
std::atomic_bool g_dropPipelines=false,g_pipelineThreadEnd=false,g_gpuCachePrunePending=false;
PipelineRef g_lastPipelineRef=UINT64_MAX;uint32_t g_lastPipelineStampFrame=0;bool g_lastPipelineValid=false;
std::mutex g_pipelineMutex,g_recordedUsesMutex;
struct CachedPipeline {uint32_t lastUsedFrame=0;};
std::unordered_map<PipelineRef,CachedPipeline> g_pipelines;
std::unordered_set<PipelineRef> g_pendingPipelines,g_pinnedPipelines,g_knownPipelines,g_recordedUses;
std::vector<int> g_pipelineQueue,g_backgroundPipelineQueue,g_pipelineUseQueue,g_pipelinePrefetchQueue,g_prefetchFrames;
std::vector<std::thread> g_pipelineThreads;
std::condition_variable g_pipelineQueueCv,g_pipelineReadyCv;
bool g_hasPipelineThread=false,g_pipelineCacheBroken=false;unsigned g_pipelinesPerFrame=0;
uint32_t g_activePipelineBuilds=0,g_activePipelinePrefetches=0;
unsigned queuedPipelines=0,createdPipelines=0,useCalls=0,promotions=0;
enum class PipelinePriority {Normal};
void touch_pending_pipeline(PipelineRef,PipelinePriority){++promotions;}
void record_pipeline_use(PipelineRef){++useCalls;}
void stop_pipeline_cache_writer(){}
void pipeline_cache_abort(){}
''' + bodies + r'''
}
int main(){using namespace aurora::gfx;
 invalidate_last_pipeline_ref();assert(!touch_pipeline(UINT64_MAX));
 for(unsigned cycle=0;cycle<128;++cycle){
  g_pipelines.emplace(19,CachedPipeline{});assert(touch_pipeline(19));
  shutdown_pipeline_cache();assert(!touch_pipeline(19));assert(g_pipelines.empty());
 }
 g_pipelines.emplace(21,CachedPipeline{});assert(touch_pipeline(21));
 g_dropPipelines=true;assert(!touch_pipeline(UINT64_MAX));assert(g_pipelines.empty());
 assert(!touch_pipeline(21));
 g_pipelines.emplace(UINT64_MAX,CachedPipeline{});assert(touch_pipeline(UINT64_MAX));
 const auto uses=useCalls;aurora_pipeline_set_context(77);assert(touch_pipeline(UINT64_MAX));assert(useCalls==uses+1);
 g_pendingPipelines.insert(88);assert(touch_pipeline(88));assert(promotions==1);
 assert(touch_pipeline(88));assert(promotions==1);
 g_dropPipelines=true;assert(!touch_pipeline(88));
 // Revisit every boundary stamp after invalidation, as after an entire
 // uint32 frame cycle without another pipeline lookup. The ref/frame bits
 // stay unchanged: only the explicit validity bit may prevent resurrection.
 for(const uint32_t stamp : {0u,1u,UINT32_MAX-1,UINT32_MAX}){
  g_pipelineFrame=stamp;g_pipelines.emplace(UINT64_MAX,CachedPipeline{});assert(touch_pipeline(UINT64_MAX));
  shutdown_pipeline_cache();const auto invalidatedStamp=g_lastPipelineStampFrame;
  g_pipelineFrame=invalidatedStamp-1;++g_pipelineFrame;assert(g_pipelineFrame==invalidatedStamp);
  assert(!touch_pipeline(UINT64_MAX));
 }
 // A wrapped 32-bit frame must still invalidate the old fast path.
 g_pipelineFrame=UINT32_MAX;invalidate_last_pipeline_ref();g_pipelines.emplace(3,CachedPipeline{});assert(touch_pipeline(3));
 ++g_pipelineFrame;invalidate_last_pipeline_ref();assert(touch_pipeline(3));assert(g_pipelines.at(3).lastUsedFrame==0);
 g_pipelines.emplace(4,CachedPipeline{UINT32_MAX-20});uint32_t total=0;
 assert(evict_idle_pipelines(16,&total)==1);assert(total==1);assert(touch_pipeline(3));assert(!touch_pipeline(4));
 // Invalidation at frame zero cannot create a phantom UINT64_MAX ref.
 assert(!touch_pipeline(UINT64_MAX));
 std::puts("actual_cache_invalidation shutdown_cycles=128 drop_context_eviction_wrap=1 all_ref_bits_valid=1 whole_uint32_wrap_invalidation=4");
}
'''
        out = Path(os.environ.get('PIPELINE_TOUCH_TEST_OUT', ROOT / 'build/pipeline-touch-lifecycle-host'))
        out.mkdir(exist_ok=True)
        cpp, exe = out / 'actual.cpp', out / 'run'
        cpp.write_text(unit)
        subprocess.run(['g++-14', '-std=c++20', '-O2', '-fsanitize=address,undefined',
                        str(cpp), '-o', str(exe)], check=True)
        result = subprocess.run([exe], text=True, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('shutdown_cycles=128', result.stdout)
        print(result.stdout.strip())

    def test_quiet_lookups_and_racing_drop_publication(self):
        source = Path(os.environ.get('PIPELINE_TOUCH_SOURCE', SOURCE)).read_text()
        take = function(source, 'static bool take_pipeline_drop_request(')
        unit = r'''
#include <atomic>
#include <barrier>
#include <cassert>
#include <cstdio>
#include <thread>
// Real atomic operations; observation hooks force the two critical ordering
// windows without relying on sleep or scheduler luck.
struct DropFlag {
 std::atomic_bool value{false};
 unsigned loads=0,exchanges=0;
 void (*afterLoad)()=nullptr;
 void (*afterExchange)()=nullptr;
 bool load(std::memory_order order) {
  ++loads; const bool result=value.load(order);
  if(afterLoad) afterLoad(); return result;
 }
 bool exchange(bool v,std::memory_order order) {
  ++exchanges; const bool result=value.exchange(v,order);
  if(afterExchange) afterExchange(); return result;
 }
} g_dropPipelines;
''' + take + r'''
std::barrier gate(2);
void pause_consumer() { gate.arrive_and_wait(); gate.arrive_and_wait(); }
int main() {
 for(unsigned i=0;i<1000000;++i) assert(!take_pipeline_drop_request());
 assert(g_dropPipelines.loads==1000000 && g_dropPipelines.exchanges==0);
 unsigned cases=0;
 for(unsigned repeat=0;repeat<100;++repeat) {
  // Publication between the quiet load and return must stay pending.
  g_dropPipelines.afterLoad=pause_consumer;
  std::thread producer([] {gate.arrive_and_wait(); g_dropPipelines.value.store(true,std::memory_order_release); gate.arrive_and_wait();});
  assert(!take_pipeline_drop_request()); producer.join();
  g_dropPipelines.afterLoad=nullptr;
  assert(g_dropPipelines.value.load()); assert(take_pipeline_drop_request()); assert(!take_pipeline_drop_request()); ++cases;
  // Another consumer wins between a true load and exchange: don't consume
  // the same publication twice.
  g_dropPipelines.value.store(true,std::memory_order_release);
  g_dropPipelines.afterLoad=pause_consumer;
  std::thread winner([] {gate.arrive_and_wait(); assert(g_dropPipelines.value.exchange(false,std::memory_order_acq_rel)); gate.arrive_and_wait();});
  assert(!take_pipeline_drop_request()); winner.join();
  g_dropPipelines.afterLoad=nullptr; assert(!take_pipeline_drop_request()); ++cases;
  // A new publication after consumption must survive that consumption.
  g_dropPipelines.value.store(true,std::memory_order_release);
  g_dropPipelines.afterExchange=pause_consumer;
  std::thread next([] {gate.arrive_and_wait(); g_dropPipelines.value.store(true,std::memory_order_release); gate.arrive_and_wait();});
  assert(take_pipeline_drop_request()); next.join();
  g_dropPipelines.afterExchange=nullptr;
  assert(take_pipeline_drop_request()); assert(!take_pipeline_drop_request()); ++cases;
 }
 // Release/acquire publication still makes the request's preceding writes
 // visible to the thread which consumes it.
 int published=0;
 std::thread publisher([&] {published=42; g_dropPipelines.value.store(true,std::memory_order_release);});
 while(!take_pipeline_drop_request()) std::this_thread::yield();
 assert(published==42); publisher.join();
 std::printf("quiet_lookups=1000000 quiet_exchanges=0 deterministic_races=%u release_acquire=1\n",cases);
}
'''
        out = Path(os.environ.get('PIPELINE_TOUCH_TEST_OUT', ROOT / 'build/pipeline-touch-lifecycle-host'))
        out.mkdir(exist_ok=True)
        cpp, exe = out / 'request.cpp', out / 'request'
        cpp.write_text(unit)
        subprocess.run(['g++-14', '-std=c++20', '-O2', '-pthread',
                        '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
        result = subprocess.run([exe], text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('deterministic_races=300', result.stdout)
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
