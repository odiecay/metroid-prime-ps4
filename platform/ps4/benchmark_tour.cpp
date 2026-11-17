// SPDX-License-Identifier: MIT
#include "benchmark_tour.hpp"
#include <array>
#include <chrono>
#include <cmath>
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
constexpr std::string_view PlayRequest = "PROJECT_PRIME_BENCHMARK_PLAY_V2\n";
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
    bool enabled{}, started{}, inventory{}, play{}, placementReady{}, jumpSafe{}, havePosition{};
    unsigned inputTick{}, power{}, charged{}, missiles{}, bombs{}, airFrames{}, morphFrames{}, lockFrames{}, guardStops{}, warps{};
    double distance{}, aimDegrees{}; float lastX{}, lastY{}, lastZ{}, lastYaw{}, routeYaw{}, routeLength{}, openness{};
    uint64_t setupNs{}, lastNs{}, lastTick{}, lastFrame{}; uint32_t lastWorld{}, lastArea{};
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
        "\"workload\":\"%s\"",
        event, static_cast<unsigned long long>(ns), static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(tick), static_cast<unsigned>(state.pass * Scenes.size() + state.scene + 1),
        scene.name, state.pass + 1, state.view, state.view * 90, scene.world, scene.area,
        actualWorld, actualArea, status, static_cast<unsigned long long>(state.frames), state.errors,
        state.play ? "scripted_native_input_v2" : "stationary_authored_spawn_four_yaws");
    static constexpr const char* kinds[] = {"running_aim", "beam_jump", "missile_aim", "morph_roll_bombs"};
    std::fprintf(state.log, ",\"input_source\":\"%s\",\"tour_version\":%u,\"view_kind\":\"%s\",\"dawn_call_profile\":%s,"
        "\"movement_distance_m\":%.6f,\"aim_degrees\":%.4f,\"power_projectiles\":%u,\"charged_projectiles\":%u,"
        "\"missile_projectiles\":%u,\"bomb_objects\":%u,\"airborne_frames\":%u,"
        "\"morphed_frames\":%u,\"enemy_lock_frames\":%u,\"guard_stops\":%u,"
        "\"unexpected_warps\":%u,\"route_length_m\":%.4f,\"openness_m\":%.4f,"
        "\"jump_safe\":%s,\"setup_ns\":%llu,\"position\":[%.5f,%.5f,%.5f],\"route_yaw_degrees\":%.4f}\n",
        state.play ? "virtual_gamecube_pad" : "none", state.play ? 2u : 1u, state.play ? kinds[state.view < 4 ? state.view : 3] : "fixed_yaw",
        state.play ? "true" : "false", state.distance, state.aimDegrees, state.power, state.charged, state.missiles,
        state.bombs, state.airFrames, state.morphFrames, state.lockFrames, state.guardStops,
        state.warps, state.routeLength, state.openness, state.jumpSafe ? "true" : "false",
        static_cast<unsigned long long>(state.setupNs),state.lastX,state.lastY,state.lastZ,state.routeYaw*57.295779513);
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
    if (!state.play && !queue(command)) return false;
    state.placementReady = !state.play; state.havePosition = false; state.inputTick = 0;
    state.power = state.charged = state.missiles = state.bombs = state.airFrames = state.morphFrames = 0;
    state.lockFrames = state.guardStops = state.warps = 0; state.distance = 0; state.aimDegrees = 0;
    state.phase = Phase::ViewSettle; state.settled = 0; state.deadline = ns + RoomTimeoutNs;
    return marker("warm_begin", "ok", ns, tick, frame);
}
void tick_at(uint32_t world, uint32_t area, uint64_t tick, uint64_t frame, bool ready, uint64_t ns) {
    if (!state.enabled || state.phase == Phase::Finished) return;
    state.lastNs=ns; state.lastTick=tick; state.lastFrame=frame; state.lastWorld=world; state.lastArea=area;
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
        ++state.inputTick;
        if (frame != state.lastMeasuredFrame) { ++state.frames; state.lastMeasuredFrame = frame; }
        if (ns - state.phaseStart < MeasureNs) return;
        if (state.play) {
            const bool moved = state.distance >= (state.view == 0 ? 2.0 : 1.0);
            const bool action = state.view == 0 ? state.aimDegrees >= 5 : (state.view == 1 ? state.power > 0 && (!state.jumpSafe || state.airFrames > 0) :
                state.view == 2 ? state.missiles > 0 : state.morphFrames >= 30 && state.bombs > 0);
            if (!moved || !action || state.warps) {
                scene_error("insufficient_action", ns, tick, frame, world, area); return;
            }
        }
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
    } else if (state.placementReady && ++state.settled >= ViewSettleTicks) {
        state.phase = Phase::Measuring; state.phaseStart = ns; state.frames = 0;
        state.lastMeasuredFrame = frame;
        marker("measure_begin", "ok", ns, tick, frame, world, area);
    }
}
} // namespace

bool prepare_benchmark_tour(const fs::path& root) {
    const fs::path playRequest = root / "benchmark-play.cfg";
    const bool play = fs::exists(playRequest) || fs::is_symlink(playRequest);
    const fs::path request = play ? playRequest : root / "benchmark-tour.cfg";
    if (!fs::exists(request) && !fs::is_symlink(request)) return false;
    if (state.enabled) throw std::runtime_error("Benchmark tour was already configured");
    if (fs::is_symlink(request) || !fs::is_regular_file(request) || fs::file_size(request) > 128)
        throw std::runtime_error("Invalid benchmark tour request");
    std::ifstream input(request, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    if (!input.eof() && input.fail()) throw std::runtime_error("Could not read benchmark request");
    if (text != (play ? PlayRequest : Request)) throw std::runtime_error("Invalid benchmark request marker");
    const char* user = std::getenv("MP_USER_PATH");
    const fs::path settings = user ? fs::path(user) / "port_settings.ini" : fs::path{};
    if (settings.empty() || fs::is_symlink(settings) || !fs::is_regular_file(settings) || fs::file_size(settings) > 1024 * 1024)
        throw std::runtime_error("Benchmark requires the current port_settings.ini for identical graphics settings");
    directory(root / "benchmarks"); const auto base = root / "benchmarks" / (play ? "play-v2" : "tour-v1");
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
    state.play = play;
    state.session = prime_ps4_trace_session_id();
    state.runId = state.session + (play ? "-play-" : "-tour-") + std::to_string(now_ns());
    environment("MP_USER_PATH", (base / "user").string());
    environment("MP_CACHE_PATH", (base / "cache").string());
    environment("MESA_SHADER_CACHE_DIR", (base / "cache" / "mesa").string());
    environment("MP_PS4_LOG_ROOT", state.logRoot.string());
    environment("MP_PS4_PROFILE_LOG", (state.logRoot / "frame-profile.jsonl").string());
    environment("MP_FAST_BOOT", "1"); environment("MP_BOOT_WORLD", "39F2DE28:2043C96E");
    environment("MP_GODMODE", "1"); environment("MP_UPDATE_CHECK", "0");
    // No network console; the in-process queue is available without MP_CONSOLE.
    unsetenv("MP_CONSOLE");
    if (play) environment("MP_DAWN_CALL_PROFILE", "1");
    state.enabled = true;
    return true;
}
bool benchmark_tour_enabled() noexcept { return state.enabled && state.phase != Phase::Finished; }
fs::path benchmark_tour_log_root() { return state.logRoot; }
bool benchmark_play_enabled() noexcept { return benchmark_tour_enabled() && state.play; }
BenchmarkPlayControl benchmark_play_control() noexcept {
    return {benchmark_play_enabled(), state.phase == Phase::ViewSettle && !state.placementReady,
        state.phase == Phase::Measuring, static_cast<unsigned>(state.pass * Scenes.size() + state.scene + 1),
        state.view, state.inputTick};
}
void benchmark_play_observe(const BenchmarkPlayObservation& p) {
    if (!benchmark_play_enabled() || state.phase != Phase::Measuring) return;
    if (!p.valid || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.yaw)) { ++state.warps; return; }
    if (state.havePosition) {
        const double dx=p.x-state.lastX,dy=p.y-state.lastY,dz=p.z-state.lastZ;
        const double distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        if (distance>4.0) ++state.warps; else state.distance+=distance;
        state.aimDegrees+=std::fabs(std::remainder(double(p.yaw)-state.lastYaw,6.283185307179586))*57.295779513;
    }
    state.havePosition=true;state.lastX=p.x;state.lastY=p.y;state.lastZ=p.z;state.lastYaw=p.yaw;
    state.power+=p.power;state.charged+=p.charged;state.missiles+=p.missiles;state.bombs+=p.bombs;
    state.airFrames+=p.airborne;state.morphFrames+=p.morphed;state.lockFrames+=p.lockedEnemy;state.guardStops+=p.guardStops;
}
void benchmark_play_placement(bool valid,float x,float y,float z,float length,float openness,bool jumpSafe,uint64_t setupNs,float yaw) {
    if (!benchmark_play_enabled() || state.phase != Phase::ViewSettle) return;
    state.routeLength=length;state.openness=openness;state.jumpSafe=jumpSafe;state.setupNs=setupNs;
    state.lastX=x;state.lastY=y;state.lastZ=z;state.lastYaw=yaw;state.routeYaw=yaw;
    if (!valid) { scene_error("no_safe_route",state.lastNs,state.lastTick,state.lastFrame,state.lastWorld,state.lastArea);return; }
    state.placementReady=true;state.settled=0;
    marker("placement", "ok", state.lastNs,state.lastTick,state.lastFrame,state.lastWorld,state.lastArea);
}
void benchmark_play_fail(const char* reason) {
    if (benchmark_play_enabled()) scene_error(reason,state.lastNs,state.lastTick,state.lastFrame,state.lastWorld,state.lastArea);
}

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
