// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <filesystem>

namespace prime {
// Call after applying the candidate paths, before settings or cache startup.
// Missing opt-in file performs no writes. Invalid explicit requests throw.
bool prepare_benchmark_tour(const std::filesystem::path& profileRoot);
bool benchmark_tour_enabled() noexcept;
std::filesystem::path benchmark_tour_log_root();
#ifdef PRIME_BENCHMARK_TEST
void benchmark_tour_reset_for_testing();
void benchmark_tour_log_failure_for_testing();
void benchmark_tour_tick_at_for_testing(uint32_t world, uint32_t mrea,
                                      uint64_t simulationTick, uint64_t traceFrame,
                                      bool ready, uint64_t monotonicNs);
#endif
}

// Call once per simulation update after PortConsoleTick. 'ready' means the
// requested room is constructed and normal first-person gameplay is usable.
// Pending asynchronous loads during measurement remain part of its real cost.
extern "C" void prime_ps4_benchmark_tick(uint32_t world, uint32_t mrea,
                                        uint64_t simulationTick, uint64_t traceFrame,
                                        bool ready);
