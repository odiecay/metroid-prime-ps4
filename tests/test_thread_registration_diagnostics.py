"""Read-only native scheduling queries run once when a trace ring is acquired."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ThreadRegistrationDiagnosticsTests(unittest.TestCase):
    def test_actual_observer_queries_once_and_keeps_raw_failure_outputs(self):
        source = (ROOT / 'platform/ps4/frame_trace_writer.cpp').read_text()
        start = source.index('static void trace_registration_observed(')
        opening = source.index('{', start)
        depth, end = 1, opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        observer = source[start:end]
        self.assertNotIn('Setprio', observer)
        self.assertNotIn('Setaffinity', observer)
        self.assertIn('extern void prime_native_getaffinity(', observer)
        code = r'''
#define PRIME_TRACE_TEST 1
#include <aurora/frame_trace.hpp>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <thread>
static std::atomic<unsigned> priorityQueries=0,affinityQueries=0,selfQueries=0;
static std::atomic<bool> fail=false;
extern "C" void* prime_native_pthread_self(){++selfQueries;return reinterpret_cast<void*>(uintptr_t(0x1234));}
extern "C" int32_t scePthreadGetprio(void* thread,int32_t* output){
 assert(thread==reinterpret_cast<void*>(uintptr_t(0x1234)));++priorityQueries;
 if(fail)return 13;*output=651;return 0;
}
extern "C" void scePthreadGetaffinity(void* thread,uint64_t* output){
 assert(thread==reinterpret_cast<void*>(uintptr_t(0x1234)));++affinityQueries;
 if(!fail)*output=0x3f;
}
''' + observer + r'''
static uint64_t seenEvents=0;
static void drain(){for(unsigned i=0;i<aurora::trace::MaxThreads;++i)aurora::trace::drain(i,[](const auto&){++seenEvents;});}
int main(){
 using namespace aurora::trace;
 registration_observer=trace_registration_observed;
 for(unsigned n=0;n<500;++n){begin(Main,n+1);transition(Main,"work",now_ns());end(Main);drain();}
 assert(priorityQueries==1 && affinityQueries==1 && selfQueries==1);
 fail=true;
 // A new thread claims one ring and queries once, even across many events.
 std::thread worker([]{for(unsigned n=0;n<500;++n)count("worker",n,Render);});worker.join();drain();
 assert(priorityQueries==2 && affinityQueries==2 && selfQueries==2);
 // Retired ring reuse is another acquisition, so log that thread's own state.
 std::thread again([]{count("worker",1,Render);});again.join();drain();
 assert(priorityQueries==3 && affinityQueries==3 && selfQueries==3);
 assert(unregisteredDrops==0);
 for(const auto& ring:rings)assert(ring.dropped==0);
 assert(seenEvents==2001);
 std::printf("actual_registration ring_acquisitions=3 thread_queries=3 repeated_events=%llu setters=0\n",
             static_cast<unsigned long long>(seenEvents));
}
'''
        out = ROOT / 'build/thread-registration-diagnostics-tests'
        out.mkdir(exist_ok=True)
        unit = out / 'actual_observer.cpp'
        unit.write_text(code)
        for sanitized in (False, True):
            binary = out / ('sanitized' if sanitized else 'normal')
            command = ['g++-14', '-std=c++20', '-O2', '-pthread',
                       '-I' + str(ROOT / 'game/MetroidPrimePort/extern/aurora/include'),
                       str(unit), '-o', str(binary)]
            if sanitized:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('ring_acquisitions=3 thread_queries=3', result.stdout)
            self.assertEqual(result.stderr.count('orbis: trace registration:'), 3)
            self.assertEqual(result.stderr.count('priority_rc=0 priority_raw=651'), 1)
            self.assertEqual(result.stderr.count('priority_rc=13 priority_raw=-2147483648'), 2)
            self.assertEqual(result.stderr.count('affinity_status=unavailable_sdk_void'), 3)
            self.assertEqual(result.stderr.count('affinity_raw=ffffffffffffffff affinity_sentinel_changed=0'), 2)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
