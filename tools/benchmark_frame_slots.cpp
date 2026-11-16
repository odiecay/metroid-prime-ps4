// Controlled HOST handoff benchmark using Aurora's production slot pool.
// Measures notification latency/wakeups, not game FPS or GPU/display completion.
#include "gfx/render_worker.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

static int64_t Now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

struct Result {
  std::vector<int64_t> delays;
  uint64_t waits = 0;
};

static void Trial(bool eventDriven, Result& result) {
  aurora::gfx::render_worker::FrameSlotPool slots{1};
  const size_t owned = slots.acquire();
  std::atomic_int64_t releasedAt = 0;
  std::jthread worker([&] {
    std::this_thread::sleep_for(2ms);
    releasedAt.store(Now(), std::memory_order_release);
    slots.release(owned);
  });
  auto available = slots.try_acquire();
  while (!available) {
    ++result.waits;
    if (eventDriven) {
      available = slots.acquire_for(1ms);
    } else {
      std::this_thread::sleep_for(100us);
      available = slots.try_acquire();
    }
  }
  result.delays.push_back(Now() - releasedAt.load(std::memory_order_acquire));
}

static void Print(const char* name, Result& result) {
  std::sort(result.delays.begin(), result.delays.end());
  const size_t n = result.delays.size();
  std::cout << '"' << name << "\":{\"samples\":" << n << ",\"wait_calls\":" << result.waits
            << ",\"median_handoff_ns\":" << result.delays[n / 2]
            << ",\"p95_handoff_ns\":" << result.delays[(n * 95 - 1) / 100]
            << ",\"worst_handoff_ns\":" << result.delays.back() << '}';
}

int main() {
  Result polling, eventDriven;
  for (unsigned i = 0; i < 400; ++i) {
    // Alternate order to reduce host-load ordering bias.
    if (i & 1) {
      Trial(true, eventDriven);
      Trial(false, polling);
    } else {
      Trial(false, polling);
      Trial(true, eventDriven);
    }
  }
  std::cout << '{';
  Print("poll_100us", polling);
  std::cout << ',';
  Print("completion_wait_1ms", eventDriven);
  std::cout << ",\"limitation\":\"Controlled host slot-release handoff; not PS4 frame-time, FPS or GPU/display completion evidence\"}\n";
}
