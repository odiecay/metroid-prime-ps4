#include "gx/fifo.hpp"
#include "gx/command_processor.hpp"
#include <aurora/frame_trace.hpp>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>


#ifdef PRIME_FIFO_RESERVE_TEST
static std::atomic<bool> failReserve{false},holdDecoder{false},decoderEntered{false},releaseDecoder{false};
static std::atomic<unsigned> reserveAttempts{0},growths{0};
static bool wholeBatch=false;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n) {
  if(n==4*1024*1024) { ++reserveAttempts;if(failReserve.exchange(false))return nullptr; }
  return __real_malloc(n);
}
extern "C" void* __wrap_realloc(void* p,size_t n) { ++growths;return __real_realloc(p,n); }
#endif

namespace aurora {
AuroraConfig g_config{};
void log_internal(AuroraLogLevel, const char*, const char*, unsigned int) noexcept {}
}
auto fmt::formatter<AuroraLogLevel>::format(AuroraLogLevel level, format_context& ctx) const
    -> format_context::iterator { return fmt::format_to(ctx.out(), "{}", static_cast<int>(level)); }

namespace aurora::gx::fifo {
// Controlled decoding boundary; thread, buffers, publication, completion,
// profiling and frame reset all use the actual production FIFO worker.
ProcessResult process(const uint8_t* data, uint32_t bytes) noexcept {
#ifdef PRIME_FIFO_RESERVE_TEST
  if(holdDecoder.exchange(false)) {
    decoderEntered.store(true,std::memory_order_release);decoderEntered.notify_all();
    releaseDecoder.wait(false,std::memory_order_acquire);
    for(unsigned i=0;i<bytes;++i)assert(data[i]==0x5a);
  }
  if(wholeBatch)return {bytes,false};
#else
  (void)data;
#endif
  return {std::min(bytes,3u),false};
}
void clear_draw_cache() noexcept {}
}

static std::atomic<uint64_t> reads{0};
static uint64_t AvailableClock() noexcept { return reads.fetch_add(1) * 100; }
static uint64_t UnsupportedClock() noexcept { ++reads;return UINT64_MAX; }

static std::map<std::string, uint64_t> RunFrame(uint64_t frame, uint64_t (*clock)() noexcept) {
  namespace trace = aurora::trace;
  namespace fifo = aurora::gx::fifo;
  trace::cpu_clock = clock;
  trace::begin(trace::Main, frame);
  fifo::begin_frame();
  const uint64_t before = reads.load();
  const uint8_t payload[8] = {};
  fifo::write_data(payload, sizeof(payload));
  fifo::drain();
  // Eight bytes consumed in three partial batches. The main frame's own CPU
  // samples are outside this interval; no CPU reads occur per decoded command.
  assert(reads.load() - before == (clock ? 6 : 0));
  fifo::end_frame();
  trace::end(trace::Main);
  std::map<std::string, uint64_t> counters;
  for (unsigned thread = 0; thread < trace::MaxThreads; ++thread) {
    trace::drain(thread, [&](const trace::Event& event) {
      if (event.kind == trace::Counter) {
        assert(event.lane == trace::Main && event.frame == frame);
        counters[event.name] += event.value;
      }
    });
  }
  assert(counters["GX FIFO decoder batches"] == 3);
  assert(counters["GX FIFO decoder bytes"] == 8);
  assert(counters["GX FIFO decoder wall ns"] > 0);
  return counters;
}

#ifdef PRIME_FIFO_RESERVE_TEST
static void ReserveChecks() {
  namespace fifo=aurora::gx::fifo;
  wholeBatch=true;
  fifo::init();assert(fifo::detail::sBufferCapacity==4*1024*1024);
  const auto* initial=fifo::get_buffer_data();const auto before=growths.load();
  const std::vector<uint8_t> typical(2*1024*1024,0x5a);
  for(unsigned frame=0;frame<33;++frame) {
    fifo::begin_frame();fifo::write_data(typical.data(),typical.size());fifo::publish();fifo::drain();fifo::end_frame();
    assert(fifo::get_buffer_data()==initial&&fifo::detail::sBufferCapacity==4*1024*1024);
  }
  assert(growths.load()==before);
  // Typical append is nonblocking even while the real decoder owns its mutex.
  fifo::begin_frame();const uint8_t first[8]={0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a,0x5a};
  fifo::write_data(first,sizeof(first));holdDecoder=true;releaseDecoder=false;decoderEntered=false;fifo::publish();
  decoderEntered.wait(false,std::memory_order_acquire);
  fifo::write_data(typical.data(),typical.size());assert(growths.load()==before);
  // Oversize still uses existing grow ownership: it cannot realloc a buffer
  // while that decoder holds a pointer into it.
  const std::vector<uint8_t> oversize(5*1024*1024,0x5a);
  std::atomic<bool> starting{false},finished{false};
  std::thread writer{[&]{starting=true;starting.notify_all();fifo::write_data(oversize.data(),oversize.size());finished=true;}};
  starting.wait(false);std::this_thread::sleep_for(std::chrono::milliseconds(30));
  assert(!finished.load());assert(growths.load()==before);
  releaseDecoder.store(true,std::memory_order_release);releaseDecoder.notify_all();writer.join();
  assert(finished.load()&&growths.load()==before+1);assert(fifo::detail::sBufferCapacity>=typical.size()+oversize.size()+sizeof(first));
  fifo::drain();fifo::end_frame();fifo::shutdown();
  // Fail only the bounded reserve, before a worker starts; preserve the prior
  // 64KiB allocation and normal growth path when memory is constrained.
  const auto attempts=reserveAttempts.load();failReserve=true;fifo::init();
  assert(reserveAttempts.load()==attempts+1&&!failReserve.load());assert(fifo::detail::sBufferCapacity==64*1024);
  const auto fallbackGrowth=growths.load();fifo::begin_frame();fifo::write_data(typical.data(),typical.size());
  assert(growths.load()==fallbackGrowth+1);fifo::drain();fifo::end_frame();fifo::shutdown();wholeBatch=false;
  std::puts("native4MiB FIFO reserve:33x2MiB frames no growth; busy-worker append no wait; oversize ownership held; allocation-failure64KiB fallback passed");
}
#endif
int main() {
  namespace fifo = aurora::gx::fifo;
#ifdef PRIME_FIFO_RESERVE_TEST
  ReserveChecks();
  for(unsigned thread=0;thread<aurora::trace::MaxThreads;++thread)
    aurora::trace::drain(thread,[](const aurora::trace::Event&){});
#endif
  fifo::init();
  const auto supported = RunFrame(1, AvailableClock);
  assert(supported.at("GX FIFO decoder CPU samples") == 3);
  assert(supported.at("GX FIFO decoder CPU ns") == 300);
  const auto unsupported = RunFrame(2, UnsupportedClock);
  assert(unsupported.at("GX FIFO decoder CPU unavailable batches") == 3);
  assert(!unsupported.contains("GX FIFO decoder CPU ns"));
  const auto missing = RunFrame(3, nullptr);
  assert(missing.at("GX FIFO decoder CPU unavailable batches") == 3);
  assert(!missing.contains("GX FIFO decoder CPU ns"));
  fifo::shutdown();
  std::puts("production FIFO worker: exactly two CPU samples per partial batch, consumed bytes, main-frame attribution, unsupported clocks and frame reset passed");
}
