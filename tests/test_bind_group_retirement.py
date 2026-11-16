"""Exercise the actual production cache expiry body with owned mock GPU handles."""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function_source(text, signature):
    start = text.index(signature)
    pos = text.index('{', start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class BindGroupRetirementTests(unittest.TestCase):
    def test_actual_expiry_bounds_movie_handles_and_preserves_queued_owners(self):
        src = (ROOT / 'game/MetroidPrimePort/extern/aurora/lib/gfx/resource_cache.cpp').read_text()
        body = function_source(src, 'void expire_cached_bind_groups(')
        clear = function_source(src, 'void clear_bind_group_cache(')
        unit = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>
#define ZoneScoped
struct Texture { static inline unsigned live=0; Texture(){++live;} ~Texture(){--live;} };
struct CachedBindGroup {std::shared_ptr<Texture> bindGroup; uint32_t lastUsedFrame=0;};
static constexpr uint32_t BindGroupCacheRetainFrames=32,BindGroupCacheSweepPeriod=16;
static uint32_t producerFrame=0,g_resizeSweepFrame=UINT32_MAX,g_lastBindGroupSweepFrame=UINT32_MAX;
static std::mutex g_bindGroupCacheMutex;
static std::unordered_map<uint64_t,CachedBindGroup> g_cachedBindGroups;
''' + clear + '\n' + body + r'''
static void reset(){clear_bind_group_cache();assert(g_resizeSweepFrame==UINT32_MAX&&g_lastBindGroupSweepFrame==UINT32_MAX);producerFrame=0;assert(!Texture::live);}
int main(){
 // The producer advances two frames between paired worker callbacks. Every
 // observed producer frame is odd, although ALL finished frame IDs are visited.
 for(uint32_t done=0;done<1200;++done){
   producerFrame=(done/2+1)*2+1;
   g_cachedBindGroups.emplace(done,CachedBindGroup{std::make_shared<Texture>(),done});
   expire_cached_bind_groups(done);
   if(g_cachedBindGroups.size()>49){
     std::fprintf(stderr,"unbounded movie retention: completed=%u producer=%u cached=%zu live=%u\n",
                  done,producerFrame,g_cachedBindGroups.size(),Texture::live);
     return 2;
   }
 }
 std::printf("odd-only producer samples: 1200 completed frames, %zu handles retained (limit49)\n",g_cachedBindGroups.size());
 reset();
 // Entries belonging to queued future frames cannot underflow their age and
 // expire. The completed frame is the retirement clock, not the producer.
 producerFrame=1000;
 auto queued=std::make_shared<Texture>();
 g_cachedBindGroups.emplace(1,CachedBindGroup{queued,1000});
 for(uint32_t done=0;done<1000;++done){expire_cached_bind_groups(done);assert(g_cachedBindGroups.count(1));}
 queued.reset();g_cachedBindGroups.clear();reset();
 // A dropped cache reference does not release another frame's owned GPU handle.
 auto submittedFrameOwner=std::make_shared<Texture>();
 std::weak_ptr<Texture> weak=submittedFrameOwner;
 g_cachedBindGroups.emplace(7,CachedBindGroup{submittedFrameOwner,0});
 producerFrame=65;
 expire_cached_bind_groups(32);assert(g_cachedBindGroups.count(7));
 expire_cached_bind_groups(48);assert(!g_cachedBindGroups.count(7));
 assert(!weak.expired() && Texture::live==1);
 submittedFrameOwner.reset();assert(weak.expired());reset();
 // Counter wrap still expires old completed-frame entries, while future ones
 // across the wrap and the startup/shutdown sentinel remain protected.
 producerFrame=33;
 g_cachedBindGroups.emplace(1,CachedBindGroup{std::make_shared<Texture>(),UINT32_MAX-31});
 g_cachedBindGroups.emplace(2,CachedBindGroup{std::make_shared<Texture>(),34});
 expire_cached_bind_groups(UINT32_MAX);assert(g_cachedBindGroups.size()==2);
 expire_cached_bind_groups(16);assert(!g_cachedBindGroups.count(1)&&g_cachedBindGroups.count(2));
 g_cachedBindGroups.clear();reset();
 // Completed IDs need not hit an exact multiple: elapsed completion progress
 // still retires an idle entry when callbacks jump between sampled epochs.
 producerFrame=101;
 g_cachedBindGroups.emplace(9,CachedBindGroup{std::make_shared<Texture>(),0});
 for(uint32_t done:{1u,18u,39u})expire_cached_bind_groups(done);
 assert(g_cachedBindGroups.empty());reset();
 // The existing resize retirement remains driven by its completed-frame cut-off.
 producerFrame=21;g_resizeSweepFrame=20;
 g_cachedBindGroups.emplace(1,CachedBindGroup{std::make_shared<Texture>(),18});
 g_cachedBindGroups.emplace(2,CachedBindGroup{std::make_shared<Texture>(),19});
 g_cachedBindGroups.emplace(3,CachedBindGroup{std::make_shared<Texture>(),21});
 expire_cached_bind_groups(18);assert(g_cachedBindGroups.size()==3);
 expire_cached_bind_groups(19);assert(!g_cachedBindGroups.count(1));
 assert(g_cachedBindGroups.count(2)&&g_cachedBindGroups.count(3));
 g_cachedBindGroups.clear();reset();
 std::puts("actual production expiry: cadence, future frames, owners, wrap and resize passed");
}
'''
        out = ROOT / 'build/bind-group-retirement-tests'
        out.mkdir(exist_ok=True)
        cpp = out / 'actual_expiry.cpp'
        cpp.write_text(unit)
        exe = out / 'actual_expiry'
        subprocess.run(['g++-14', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(cpp), '-o', str(exe)], check=True)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        for _ in range(20):
            run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        print(run.stdout, end='')


if __name__ == '__main__':
    unittest.main()
