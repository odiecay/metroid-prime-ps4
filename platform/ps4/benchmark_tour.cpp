// SPDX-License-Identifier: MIT
#include "benchmark_tour.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

// Existing in-process console queue: no listener or external service is used.
bool PortConsoleRun(std::string_view line);
extern "C" const char* prime_ps4_trace_session_id() noexcept;

namespace prime {
namespace {
namespace fs = std::filesystem;
constexpr std::string_view Request = "PROJECT_PRIME_BENCHMARK_TOUR_V1\n";
constexpr uint64_t Second = 1000000000ull;
constexpr unsigned WarmTicks = 180, ViewSettleTicks = 60, Passes = 2;
constexpr uint64_t MeasureNs = 12 * Second, RoomTimeoutNs = 120 * Second;
struct Scene { const char* name; uint32_t world, area; };
// The pinned port_ap_world_data.inc room table supplies these exact IDs.
constexpr std::array<Scene, 6> Scenes{{
    {"Tallon Canyon",       0x39F2DE28, 0x2043C96E},
    {"Main Plaza",          0x83F6FF6F, 0xD5CDB809},
    {"Fiery Shores",        0x3EF8237C, 0xF5EF1862},
    {"Phendrana Shorelines",0xA8BE6291, 0xF7285979},
    {"Research Lab Hydra",  0xA8BE6291, 0x43E4CC25},
    {"Main Quarry",         0xB1AC4D65, 0x643D038F},
}};
enum class Phase { Waiting, ViewSettle, Measuring, Finished };
struct State {
    bool enabled{}, started{}, inventory{};
    unsigned scene{}, pass{}, view{}, settled{}, errors{};
    Phase phase = Phase::Waiting;
    uint64_t phaseStart{}, deadline{}, frames{}, lastMeasuredFrame{};
    fs::path logRoot;
    std::string runId, session;
    FILE* log{};
} state;

uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void json_text(FILE* file, const char* text) {
    std::fputc('"', file);
    for (; *text; ++text) {
        if (*text == '"' || *text == '\\') std::fputc('\\', file);
        if (static_cast<unsigned char>(*text) >= 32) std::fputc(*text, file);
    }
    std::fputc('"', file);
}
bool marker(const char* event, const char* status, uint64_t ns, uint64_t tick,
            uint64_t frame, uint32_t actualWorld = 0, uint32_t actualArea = 0) {
    if (!state.log) return false;
    const auto& scene = Scenes[state.scene];
    std::fprintf(state.log, "{\"schema\":1,\"type\":\"benchmark_tour\",\"session_id\":");
    json_text(state.log, state.session.c_str());
    std::fputs(",\"run_id\":", state.log); json_text(state.log, state.runId.c_str());
    std::fprintf(state.log, ",\"event\":\"%s\",\"monotonic_ns\":%llu,\"frame\":%llu,"
        "\"simulation_tick\":%llu,\"visit\":%u,\"scene\":\"%s\",\"pass\":%u,"
        "\"view\":%u,\"yaw_degrees\":%u,\"world\":\"%08X\",\"mrea\":\"%08X\","
        "\"actual_world\":\"%08X\",\"actual_mrea\":\"%08X\",\"status\":\"%s\","
        "\"observed_frames\":%llu,\"failed_visits\":%u,\"assisted_inventory\":true,"
        "\"invulnerable\":true,\"cache_policy\":\"isolated_persistent\","
        "\"workload\":\"stationary_authored_spawn_four_yaws\"}\n",
        event, static_cast<unsigned long long>(ns), static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(tick), static_cast<unsigned>(state.pass * Scenes.size() + state.scene + 1),
        scene.name, state.pass + 1, state.view, state.view * 90, scene.world, scene.area,
        actualWorld, actualArea, status, static_cast<unsigned long long>(state.frames), state.errors);
    if (std::ferror(state.log) || std::fflush(state.log) != 0) {
        std::fprintf(stderr, "[benchmark-tour] marker write failed; stopped without acceptance (run %s)\n", state.runId.c_str());
        state.phase = Phase::Finished; ++state.errors;
        std::fclose(state.log); state.log = nullptr;
        return false;
    }
    return true;
}
void directory(const fs::path& path) {
    if (fs::is_symlink(path)) throw std::runtime_error("Benchmark path is a symlink");
    if (fs::exists(path) && !fs::is_directory(path))
        throw std::runtime_error("Benchmark path is not a directory");
    fs::create_directory(path);
}
void environment(const char* key, const std::string& value) {
    if (setenv(key, value.c_str(), 1)) throw std::runtime_error("Could not set benchmark environment");
}
bool queue(const std::string& text) { return PortConsoleRun(text); }
void finish(uint64_t ns, uint64_t tick, uint64_t frame) {
    marker(state.errors ? "incomplete" : "complete", state.errors ? "failed_visits" : "ok", ns, tick, frame);
    state.phase = Phase::Finished;
    if (state.log) std::fclose(state.log);
    state.log = nullptr;
}
void next_scene(uint64_t ns, uint64_t tick, uint64_t frame) {
    if (state.phase == Phase::Finished) return;
    if (++state.scene == Scenes.size()) {
        state.scene = 0;
        if (++state.pass == Passes) {
            // Keep the final marker's visit/scene identity within the actual tour.
            state.pass = Passes - 1; state.scene = Scenes.size() - 1;
            finish(ns, tick, frame); return;
        }
    }
    state.view = 0; state.settled = 0; state.frames = 0;
    state.phase = Phase::Waiting; state.deadline = ns + RoomTimeoutNs;
    char line[64]; const auto& scene = Scenes[state.scene];
    std::snprintf(line, sizeof(line), "warp %08X %08X", scene.world, scene.area);
    if (!queue(line)) {
        ++state.errors; marker("aborted", "queue_full", ns, tick, frame);
        state.phase = Phase::Finished; if (state.log) std::fclose(state.log); state.log = nullptr; return;
    }
    marker("wait_ready", "ok", ns, tick, frame);
}
void scene_error(const char* why, uint64_t ns, uint64_t tick, uint64_t frame,
                 uint32_t world, uint32_t area) {
    if (state.phase == Phase::Finished) return;
    if (state.phase == Phase::Measuring && !marker("measure_end", why, ns, tick, frame, world, area)) return;
    ++state.errors; if (!marker("scene_error", why, ns, tick, frame, world, area)) return;
    next_scene(ns, tick, frame);
}
bool grant_inventory() {
    // Inventory only; no artifact relays, boss deaths, layers, physics or saves.
    for (const char* line : {"give PowerBeam 1", "give ChargeBeam 1", "give CombatVisor 1",
         "give ScanVisor 1", "give ThermalVisor 1", "give XRayVisor 1", "give MorphBall 1",
         "give MorphBallBombs 1", "give BoostBall 1", "give SpiderBall 1", "give SpaceJumpBoots 1",
         "give GrappleBeam 1", "give VariaSuit 1", "give GravitySuit 1", "give IceBeam 1",
         "give WaveBeam 1", "give PlasmaBeam 1", "give SuperMissile 1", "give Missiles 250",
         "give PowerBombs 8", "give EnergyTanks 14", "refill all"}) {
        if (!queue(line)) return false;
    }
    return true;
}
bool begin_view(uint64_t ns, uint64_t tick, uint64_t frame) {
    char command[32]; std::snprintf(command, sizeof(command), "face %u", state.view * 90);
    if (!queue(command)) return false;
    state.phase = Phase::ViewSettle; state.settled = 0; state.deadline = ns + RoomTimeoutNs;
    return marker("warm_begin", "ok", ns, tick, frame);
}
void tick_at(uint32_t world, uint32_t area, uint64_t tick, uint64_t frame, bool ready, uint64_t ns) {
    if (!state.enabled || state.phase == Phase::Finished) return;
    if (!state.started) {
        state.started = true; state.deadline = ns + RoomTimeoutNs;
        if (!marker("start", "ok", ns, tick, frame, world, area) ||
            !marker("wait_ready", "ok", ns, tick, frame, world, area)) return;
    }
    const auto& scene = Scenes[state.scene];
    const bool here = world == scene.world && area == scene.area;
    if (state.phase != Phase::Measuring && ns >= state.deadline) {
        scene_error("timeout", ns, tick, frame, world, area); return;
    }
    if (state.phase == Phase::Measuring) {
        if (!ready || !here) { scene_error("lost_ready", ns, tick, frame, world, area); return; }
        if (frame != state.lastMeasuredFrame) { ++state.frames; state.lastMeasuredFrame = frame; }
        if (ns - state.phaseStart < MeasureNs) return;
        if (!marker("measure_end", "ok", ns, tick, frame, world, area)) return;
        if (++state.view == 4) next_scene(ns, tick, frame);
        else if (!begin_view(ns, tick, frame)) scene_error("queue_full", ns, tick, frame, world, area);
        return;
    }
    if (!ready || !here) { state.settled = 0; return; }
    if (!state.inventory) {
        if (!grant_inventory()) { scene_error("queue_full", ns, tick, frame, world, area); return; }
        state.inventory = true;
    }
    if (state.phase == Phase::Waiting) {
        if (++state.settled < WarmTicks) return;
        if (!marker("ready", "ok", ns, tick, frame, world, area)) return;
        if (!begin_view(ns, tick, frame)) scene_error("queue_full", ns, tick, frame, world, area);
    } else if (++state.settled >= ViewSettleTicks) {
        state.phase = Phase::Measuring; state.phaseStart = ns; state.frames = 0;
        state.lastMeasuredFrame = frame;
        marker("measure_begin", "ok", ns, tick, frame, world, area);
    }
}
} // namespace

bool prepare_benchmark_tour(const fs::path& root) {
    const fs::path request = root / "benchmark-tour.cfg";
    if (!fs::exists(request) && !fs::is_symlink(request)) return false;
    if (state.enabled) throw std::runtime_error("Benchmark tour was already configured");
    if (fs::is_symlink(request) || !fs::is_regular_file(request) || fs::file_size(request) > 128)
        throw std::runtime_error("Invalid benchmark tour request");
    std::ifstream input(request, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    if (!input.eof() && input.fail()) throw std::runtime_error("Could not read benchmark request");
    if (text != Request) throw std::runtime_error("Benchmark tour request must be PROJECT_PRIME_BENCHMARK_TOUR_V1");
    const char* user = std::getenv("MP_USER_PATH");
    const fs::path settings = user ? fs::path(user) / "port_settings.ini" : fs::path{};
    if (settings.empty() || fs::is_symlink(settings) || !fs::is_regular_file(settings) || fs::file_size(settings) > 1024 * 1024)
        throw std::runtime_error("Benchmark requires the current port_settings.ini for identical graphics settings");
    directory(root / "benchmarks"); const auto base = root / "benchmarks" / "tour-v1";
    directory(base); directory(base / "user"); directory(base / "cache"); directory(base / "logs");
    directory(base / "cache" / "mesa");
    for (const auto& item : fs::recursive_directory_iterator(base))
        if (item.is_symlink()) throw std::runtime_error("Benchmark namespace contains a symlink");
    const auto copy = base / "user" / "port_settings.ini";
    if (fs::is_symlink(copy) || (fs::exists(copy) && !fs::is_regular_file(copy)))
        throw std::runtime_error("Invalid benchmark settings destination");
    fs::copy_file(settings, copy, fs::copy_options::overwrite_existing);
    state.logRoot = base / "logs";
    const auto log = state.logRoot / "benchmark-tour.jsonl";
    if (fs::is_symlink(log) || (fs::exists(log) && !fs::is_regular_file(log)))
        throw std::runtime_error("Invalid benchmark log destination");
    state.log = std::fopen(log.c_str(), "ab");
    if (!state.log) throw std::runtime_error("Could not open benchmark tour log");
    state.session = prime_ps4_trace_session_id();
    state.runId = state.session + "-tour-" + std::to_string(now_ns());
    environment("MP_USER_PATH", (base / "user").string());
    environment("MP_CACHE_PATH", (base / "cache").string());
    environment("MESA_SHADER_CACHE_DIR", (base / "cache" / "mesa").string());
    environment("MP_PS4_LOG_ROOT", state.logRoot.string());
    environment("MP_PS4_PROFILE_LOG", (state.logRoot / "frame-profile.jsonl").string());
    environment("MP_FAST_BOOT", "1"); environment("MP_BOOT_WORLD", "39F2DE28:2043C96E");
    environment("MP_GODMODE", "1"); environment("MP_UPDATE_CHECK", "0");
    // No network console; the in-process queue is available without MP_CONSOLE.
    unsetenv("MP_CONSOLE");
    state.enabled = true;
    return true;
}
bool benchmark_tour_enabled() noexcept { return state.enabled && state.phase != Phase::Finished; }
fs::path benchmark_tour_log_root() { return state.logRoot; }
#ifdef PRIME_BENCHMARK_TEST
void benchmark_tour_reset_for_testing() {
    if (state.log) std::fclose(state.log);
    state = {};
}
void benchmark_tour_log_failure_for_testing() {
    if (!state.log || !std::freopen("/dev/full", "wb", state.log))
        throw std::runtime_error("Could not create the test failure sink");
}
void benchmark_tour_tick_at_for_testing(uint32_t world, uint32_t mrea, uint64_t tick,
                                      uint64_t frame, bool ready, uint64_t ns) {
    tick_at(world, mrea, tick, frame, ready, ns);
}
#endif
} // namespace prime
extern "C" void prime_ps4_benchmark_tick(uint32_t world, uint32_t mrea, uint64_t tick,
                                        uint64_t frame, bool ready) {
    if (prime::benchmark_tour_enabled()) prime::tick_at(world, mrea, tick, frame, ready, prime::now_ns());
}
