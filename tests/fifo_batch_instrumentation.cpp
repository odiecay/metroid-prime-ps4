#include "gx/fifo.hpp"
#include "gx/command_processor.hpp"
#include <aurora/frame_trace.hpp>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>

namespace aurora {
AuroraConfig g_config{};
void log_internal(AuroraLogLevel, const char*, const char*, unsigned int) noexcept {}
}
auto fmt::formatter<AuroraLogLevel>::format(AuroraLogLevel level, format_context& ctx) const
    -> format_context::iterator { return fmt::format_to(ctx.out(), "{}", static_cast<int>(level)); }

namespace aurora::gx::fifo {
// Controlled decoding boundary; thread, buffers, publication, completion,
// profiling and frame reset all use the actual production FIFO worker.
ProcessResult process(const uint8_t*, uint32_t bytes) noexcept { return {std::min(bytes, 3u), false}; }
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

int main() {
  namespace fifo = aurora::gx::fifo;
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
