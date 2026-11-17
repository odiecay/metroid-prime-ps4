"""Check the actual frame-cap block and its diagnostic phase boundaries."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
AURORA = ROOT / 'game/MetroidPrimePort/extern/aurora/include'
SOURCE = ROOT / 'game/MetroidPrimePort/src/MetroidPrime/main.cpp'


class FramePacingTraceTests(unittest.TestCase):
    def test_actual_cap_waits_and_post_cap_phase_ownership(self):
        source = SOURCE.read_text()
        start = source.index('      aurora::phase::set(aurora::phase::Main, "frame pacing");')
        end = source.index('      PortMemoryLog::Tick(SDL_GetTicksNS());', start)
        end += len('      PortMemoryLog::Tick(SDL_GetTicksNS());')
        block = source[start:end]
        self.assertEqual(block.count('"frame statistics"'), 1)
        self.assertEqual(block.count('"frame diagnostics"'), 1)
        self.assertLess(block.index('"frame statistics"'), block.index('PortDebug::RecordFrame('))
        self.assertLess(block.index('PortDebug::RecordFrame('), block.index('"frame diagnostics"'))
        self.assertLess(block.index('"frame diagnostics"'), block.index('PortMemoryLog::Tick('))
        # These are the first post-cap diagnostics, so milestone/stall logging
        # must retain the diagnostics label until the real frame ends.
        tail = source[end:source.index('    } catch (...)', end)]
        self.assertNotIn('aurora::phase::set(', tail)
        self.assertIn('MP stall:', tail)
        code = r'''
#define PRIME_TRACE_TEST 1
#include <aurora/phase.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
static uint64_t virtualNow=0, coarseOversleep=0, enteredStats=0;
static int frameCap=60;static bool turbo=false;
static std::vector<uint64_t> waits;
uint64_t SDL_GetTicksNS(){return virtualNow;}
void SDL_DelayNS(uint64_t ns){waits.push_back(ns);virtualNow+=ns+coarseOversleep;}
void SDL_DelayPrecise(uint64_t ns){waits.push_back(ns);virtualNow+=ns;}
namespace PortDebug {
int FrameRateCap(){return frameCap;}bool Turbo(){return turbo;}
void RecordFrame(uint64_t work,unsigned ticks,bool presented){
 assert(!std::strcmp(aurora::phase::state(aurora::phase::Main).name.load(),"frame statistics"));
 assert(work==100 && ticks==1 && presented);enteredStats=virtualNow;virtualNow+=3000000;
}
}
namespace PortMemoryLog {
void Tick(uint64_t){
 assert(!std::strcmp(aurora::phase::state(aurora::phase::Main).name.load(),"frame diagnostics"));
 virtualNow+=6000000;
}
}
static void actual_cap(uint64_t& nextFrameDeadline){
 const uint64_t workEndNs=100,loopStartNs=0;const unsigned sTicksAdvanced=1;const bool presented=true;
''' + block + r'''
}
static unsigned frames=0;
static void exercise(uint64_t& deadline){
 waits.clear();const auto id=++frames;
 {aurora::trace::Frame frame(aurora::trace::Main,id);actual_cap(deadline);virtualNow+=2000000;}
 std::vector<const char*> labels;unsigned complete=0;
 for(unsigned i=0;i<aurora::trace::MaxThreads;++i)aurora::trace::drain(i,[&](const auto& event){
  assert(event.frame==id && event.lane==aurora::trace::Main);
  if(event.kind==aurora::trace::Phase)labels.push_back(event.name);
  if(event.kind==aurora::trace::Complete)++complete;
 });
 assert(complete==1 && labels.size()==4);
 assert(!std::strcmp(labels[0],"frame setup"));assert(!std::strcmp(labels[1],"frame pacing"));
 assert(!std::strcmp(labels[2],"frame statistics"));assert(!std::strcmp(labels[3],"frame diagnostics"));
}
int main(){
 constexpr uint64_t period=1000000000ull/60;
 uint64_t deadline=0;virtualNow=10000000;exercise(deadline);
 assert(deadline==period && waits==std::vector<uint64_t>({period-12000000,2000000}));
 assert(enteredStats==period);
 deadline=0;virtualNow=40000000;exercise(deadline);assert(waits.empty()&&deadline==40000000);
 deadline=0;virtualNow=20000000;exercise(deadline);assert(waits.empty()&&deadline==period);
 deadline=0;virtualNow=10000000;turbo=true;exercise(deadline);assert(waits.empty()&&deadline==10000000);turbo=false;
 deadline=0;virtualNow=10000000;frameCap=0;exercise(deadline);assert(waits.empty()&&deadline==10000000);frameCap=60;
 // Model a late wake-up. The source requests less than one period, while
 // actual elapsed time can exceed it; this is not a PS4 sleep measurement.
 deadline=0;virtualNow=10000000;coarseOversleep=90000000;exercise(deadline);
 assert(waits==std::vector<uint64_t>({period-12000000}));
 assert(enteredStats==period-2000000+90000000);coarseOversleep=0;
 // Repeated real trace lifetimes must not retain a previous frame's phase.
 deadline=virtualNow;
 for(unsigned n=0;n<10000;++n){
  virtualNow+=(n*4051u)%40000000;exercise(deadline);
  uint64_t requested=0;for(auto ns:waits)requested+=ns;assert(requested<=period);
 }
 assert(aurora::trace::unregisteredDrops.load()==0);
 for(const auto& ring:aurora::trace::rings)assert(ring.dropped.load()==0);
 std::printf("actual_cap frames=%u unchanged_wait_cases=6 post_cap_phases=2 drops=0\n",frames);
}
'''
        out = ROOT / 'build/frame-pacing-trace-tests'
        out.mkdir(exist_ok=True)
        unit = out / 'actual_cap.cpp'
        unit.write_text(code)
        for sanitized in (False, True):
            binary = out / ('sanitized' if sanitized else 'normal')
            command = ['g++-14', '-std=c++20', '-O2', '-I' + str(AURORA), str(unit), '-o', str(binary)]
            if sanitized:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('unchanged_wait_cases=6 post_cap_phases=2 drops=0', result.stdout)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
