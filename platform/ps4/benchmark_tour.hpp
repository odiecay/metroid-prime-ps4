// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <filesystem>
class CStateManager;

namespace prime {
// Call after applying the candidate paths, before settings or cache startup.
// Missing opt-in file performs no writes. Invalid explicit requests throw.
bool prepare_benchmark_tour(const std::filesystem::path& profileRoot);
bool benchmark_tour_enabled() noexcept;
bool benchmark_play_enabled() noexcept;
std::filesystem::path benchmark_tour_log_root();
struct BenchmarkPlayControl {
    bool active{}, placing{}, measuring{};
    unsigned visit{}, view{}, inputTick{};
    bool rehearsing{}; // Same action sequence, explicitly outside the timed interval.
};
struct BenchmarkPlayObservation {
    bool valid{}, airborne{}, morphed{}, lockedEnemy{};
    float x{}, y{}, z{}, yaw{};
    unsigned power{}, charged{}, missiles{}, bombs{}, guardStops{};
};
BenchmarkPlayControl benchmark_play_control() noexcept;
void benchmark_play_observe(const BenchmarkPlayObservation& sample);
void benchmark_play_placement(bool valid, float x, float y, float z, float length,
                              float openness, bool jumpSafe, uint64_t setupNs, float yaw = 0.f);
void benchmark_play_fail(const char* reason);
// Native adapter: observes real actors, prevalidates terrain, injects normal PAD input.
void benchmark_game_tick(CStateManager& mgr);
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
