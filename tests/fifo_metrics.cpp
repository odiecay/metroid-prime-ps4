#include "fifo_metrics.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
  using namespace aurora::gx::fifo;
  constexpr auto unavailable = std::numeric_limits<uint64_t>::max();
  BatchMetrics metrics;
  metrics.Record({1000, 200}, {2000, 900}, 128);
  metrics.Record({3000, 1000}, {6000, 1500}, 64);
  assert(metrics.batches == 2 && metrics.bytes == 192);
  assert(metrics.wallNs == 4000 && metrics.cpuNs == 1200 && metrics.cpuSamples == 2);
  // Neither missing nor decreasing CPU clocks manufacture execution time.
  metrics.Record({6000, unavailable}, {9000, unavailable}, 12);
  metrics.Record({9000, 2000}, {12000, unavailable}, 8);
  metrics.Record({12000, 2500}, {14000, 2000}, 4);
  assert(metrics.batches == 5 && metrics.bytes == 216);
  assert(metrics.cpuSamples == 2 && metrics.cpuNs == 1200 && metrics.unavailableCpu == 3);
  assert(metrics.wallNs == 12000 && metrics.invalidWall == 0);
  // A malformed wall sample is explicit, not unsigned underflow.
  metrics.Record({14000, 3000}, {10000, 3200}, 3);
  assert(metrics.invalidWall == 1 && metrics.wallNs == 12000 && metrics.cpuNs == 1400);
  metrics = {};
  assert(metrics.batches == 0 && metrics.bytes == 0 && metrics.cpuNs == 0 && metrics.wallNs == 0);
  std::puts("FIFO batch metrics: CPU/wall separation, exact consumed bytes, unavailable/decreasing clocks and reset passed");
}
