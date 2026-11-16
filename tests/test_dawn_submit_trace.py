"""Check actual Dawn submit instrumentation through queue and present call paths."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "external/dawn/src/dawn/native/vulkan/QueueVk.cpp"

class DawnSubmitTraceTests(unittest.TestCase):
    def test_each_actual_submit_is_measured_once(self):
        source = SOURCE.read_text()
        submit = source.split("MaybeError Queue::SubmitImpl(Span<CommandBufferBase* const> commands) {", 1)[1].split("\nvoid Queue::SetLabelImpl()", 1)[0].rsplit("}", 1)[0]
        pending = source.split("MaybeError Queue::SubmitPendingCommandsImpl() {", 1)[1]
        prefix = pending.split("    Device* device = ToBackend(GetDevice());", 1)[0]
        self.assertNotIn("profileSubmit", submit)
        self.assertEqual(source.count("PrimeDawnScope profileSubmit(2);"), 1)
        self.assertLess(prefix.index("if (!mRecordingContext.needsSubmit)"), prefix.index("profileSubmit"))
        # The real backend's recycling scope is declared after this timer, so
        # recycling completes before the timer callback on success/error paths.
        self.assertLess(pending.index("profileSubmit"), pending.index("absl::Cleanup recycleContext"))
        prelude = r'''
#include "prime_dawn_trace.h"
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>
#define __ORBIS__ 1
#define TRACE_EVENT_BEGIN0(...) ((void)0)
#define TRACE_EVENT_END0(...) ((void)0)
struct MaybeError {bool error=false;};
#define DAWN_TRY(expr) do {auto result=(expr);if(result.error)return result;} while(0)
struct CommandRecordingContext {bool needsSubmit=false;};
struct CommandBufferBase {MaybeError RecordCommands(CommandRecordingContext*){return {};}};
template<class T>T* ToBackend(T* p){return p;}
template<class T>using Span=std::span<T>;
unsigned recycles=0;
struct Event {unsigned id,recycles;uint64_t ns;};
std::vector<Event> events;
extern "C" void prime_ps4_dawn_duration(unsigned id,uint64_t ns){events.push_back({id,recycles,ns});}
class Queue {
public:
 CommandRecordingContext mRecordingContext;
 bool fail=false;
 CommandRecordingContext* GetPendingRecordingContext(){mRecordingContext.needsSubmit=true;return &mRecordingContext;}
 MaybeError SubmitImpl(Span<CommandBufferBase* const> commands) {
'''
        middle = r'''
 }
 MaybeError SubmitPendingCommandsImpl() {
'''
        checks = r'''
 // Fake Vulkan work after the actual production guard/timer prefix.
 struct Recycle {~Recycle(){++recycles;}} recycle;
 mRecordingContext.needsSubmit=false;
 return {fail};
 }
};
int main() {
 Queue queue;CommandBufferBase cmd;CommandBufferBase* commands[]={&cmd};
 assert(!queue.SubmitImpl(commands).error); // the main game command buffer
 assert(events.size()==2 && events[0].id==0 && events[1].id==2);
 assert(events[1].recycles==1);
 queue.mRecordingContext.needsSubmit=true; // Present's transition/semaphore submit
 assert(!queue.SubmitPendingCommandsImpl().error);
 assert(events.size()==3 && events[2].id==2 && events[2].recycles==2);
 assert(!queue.SubmitPendingCommandsImpl().error); // empty Tick: no submit/timing
 assert(events.size()==3 && recycles==2);
 queue.mRecordingContext.needsSubmit=true;queue.fail=true;
 assert(queue.SubmitPendingCommandsImpl().error); // failed work is still timed
 assert(events.size()==4 && events[3].id==2 && events[3].recycles==3);
 // Exactly two successful backend submits, with no enclosing duplicate timer.
 unsigned pendingEvents=0;for(auto e:events)if(e.id==2)++pendingEvents;
 assert(pendingEvents==3 && recycles==3);
}
'''
        out = ROOT / "build/dawn-submit-trace-host"
        out.mkdir(exist_ok=True)
        unit = out / "actual_submit_trace.cpp"
        unit.write_text(prelude + submit + middle + prefix + checks)
        program = out / "run"
        subprocess.run(["g++-14", "-std=c++20", "-O2", "-fsanitize=address,undefined",
                        "-I" + str(ROOT / "platform/ps4/compat"), str(unit), "-o", str(program)], check=True)
        result = subprocess.run([program], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == "__main__":
    unittest.main()
