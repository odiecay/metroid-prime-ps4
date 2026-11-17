// SPDX-License-Identifier: MIT
#include "benchmark_tour.hpp"
#include <aurora/gfx.h>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace fs = std::filesystem;
static std::vector<std::string> commands;
static bool rejectQueue;
static bool rejectPermissionChanges;
static unsigned permissionChanges;
extern "C" int fchmod(int fd, mode_t mode) noexcept {
    if (rejectPermissionChanges) { ++permissionChanges; errno=EPERM; return -1; }
    return static_cast<int>(::syscall(SYS_fchmod,fd,mode));
}
static uint32_t world=0x39F2DE28, area=0x2043C96E;
bool PortConsoleRun(std::string_view text) {
    if (rejectQueue) return false;
    commands.emplace_back(text);
    if (text.starts_with("warp "))
        assert(std::sscanf(commands.back().c_str(), "warp %x %x", &world, &area)==2);
    return true;
}
extern "C" const char* prime_ps4_trace_session_id() noexcept { return "host-contract-session"; }
static AuroraPipelineReadiness shaders=[] { AuroraPipelineReadiness out{};out.epoch=1;return out; }();
extern "C" void aurora_get_pipeline_readiness(AuroraPipelineReadiness* out) { *out=shaders; }
static std::string read(const fs::path& p) {
    std::ifstream input(p,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),{}};
}
static void write(const fs::path& p,const std::string& v) { std::ofstream(p,std::ios::binary)<<v; }
int main(int argc,char** argv) {
    assert(argc==3);const std::string mode=argv[1];const fs::path root=argv[2];
    const fs::path live=root/"live";fs::create_directories(live);
    const bool play=mode.starts_with("play_");
    std::string settings="render_scale=2.25\nmsaa=1\noriginal_experience=0\n";
    write(live/"port_settings.ini",settings);write(live/"memorycard.bin","PROTECTED-SAVE");
    setenv("MP_USER_PATH",live.c_str(),1);setenv("MP_CACHE_PATH",(live/"cache").c_str(),1);
    if(mode=="absent") {
        assert(!prime::prepare_benchmark_tour(root));assert(!prime::benchmark_tour_enabled());
        assert(!fs::exists(root/"benchmarks"));assert(std::string(getenv("MP_USER_PATH"))==live);
        return 0;
    }
    write(root/"benchmark-tour.cfg",mode=="invalid"?"not-a-tour\n":"PROJECT_PRIME_BENCHMARK_TOUR_V1\n");
    if(play)write(root/"benchmark-play.cfg",mode=="play_invalid"?"WRONG_VERSION\n":"PROJECT_PRIME_BENCHMARK_PLAY_V2\n");
    if(mode=="symlink") {
        fs::create_directories(root/"benchmarks");
        fs::create_directory_symlink(live,root/"benchmarks"/"tour-v1");
    }
    if(mode=="invalid"||mode=="symlink"||mode=="play_invalid") {
        bool failed=false;try { prime::prepare_benchmark_tour(root); }catch(...) {failed=true;}
        assert(failed);assert(!prime::benchmark_tour_enabled());
        assert(std::string(getenv("MP_USER_PATH"))==live);assert(read(live/"port_settings.ini")==settings);
        assert(read(live/"memorycard.bin")=="PROTECTED-SAVE");return 0;
    }
    if(mode=="play_restart") {
        const auto user=root/"benchmarks/play-v2/user";
        fs::create_directories(user);
        write(user/"port_settings.ini","STALE-BENCHMARK-SETTINGS");
        write(user/"port_settings.ini.pending","INTERRUPTED-COPY");
        rejectPermissionChanges=true;
        // Reproduce the old permission-changing overwrite before exercising
        // the real startup path against the same existing destination.
        std::error_code ec;
        assert(!fs::copy_file(live/"port_settings.ini",user/"port_settings.ini",fs::copy_options::overwrite_existing,ec));
        assert(ec.value()==EPERM && permissionChanges==1);
        // libstdc++ may truncate before chmod, unlike the pinned libc++.
        // Restore the sentinel before exercising production's atomic copy.
        write(user/"port_settings.ini","STALE-BENCHMARK-SETTINGS");
        assert(prime::prepare_benchmark_tour(root));
        assert(read(user/"port_settings.ini")==settings);
        assert(permissionChanges==1);
        prime::benchmark_tour_reset_for_testing();
        setenv("MP_USER_PATH",live.c_str(),1);
        settings+="anisotropy=16\n";write(live/"port_settings.ini",settings);
    }
    assert(prime::prepare_benchmark_tour(root));
    if(mode=="play_restart") {
        assert(permissionChanges==1);
        assert(!fs::exists(root/"benchmarks/play-v2/user/port_settings.ini.pending"));
    }
    assert(prime::benchmark_tour_enabled());const fs::path bench=root/"benchmarks"/(play?"play-v2":"tour-v1");
    assert(prime::benchmark_play_enabled()==play);
    assert(std::string(getenv("MP_USER_PATH"))==bench/"user");
    assert(std::string(getenv("MP_CACHE_PATH"))==bench/"cache");
    assert(std::string(getenv("MP_PS4_PROFILE_LOG"))==bench/"logs"/"frame-profile.jsonl");
    assert(read(bench/"user"/"port_settings.ini")==settings);
    assert(!fs::exists(bench/"user"/"memorycard.bin"));
    if(mode=="log_failure")prime::benchmark_tour_log_failure_for_testing();
    if(mode=="queue_failure")rejectQueue=true;
    uint64_t tick=0,ns=1000000000ull;bool interrupted=false;
    while(prime::benchmark_tour_enabled()&&tick<100000) {
        auto control=prime::benchmark_play_control();
        if(mode=="shader_busy") {
            shaders.queued=tick<450?3:0;shaders.background=tick<600?2:0;
            shaders.prefetchQueued=tick<630?1:0;
            shaders.prefetchActive=tick>=630&&tick<650?1:0;
            shaders.active=tick>=600&&tick<620?1:0;
            shaders.pending=shaders.queued+shaders.background+(shaders.active&&tick<610?1:0);
            shaders.completed=tick>=620?6:0;
        }
        if(mode=="shader_failure")shaders.failures=1;
        if(mode=="shader_timeout")shaders.active=1;
        if(control.measuring&&control.inputTick==100&&
           (mode=="shader_unstable"||(mode=="shader_retry"&&!interrupted))) {
            ++shaders.completed;interrupted=true;
        }
        if(control.placing)prime::benchmark_play_placement(mode!="play_bad_pose",0,0,0,4,20,true,100);
        if(control.measuring || control.rehearsing) {
            prime::BenchmarkPlayObservation sample;sample.valid=true;
            if(mode!="play_no_actions") {
                sample.x=control.inputTick*.01f;sample.yaw=std::sin(control.inputTick*.03f)*.15f;
                sample.airborne=control.view==1&&control.inputTick<40;sample.morphed=control.view==3;
                sample.power=control.view==1&&control.inputTick%20==0;
                sample.charged=control.view==1&&control.inputTick%120==0;
                sample.missiles=control.view==2&&control.inputTick%90==0;
                sample.bombs=control.view==3&&control.inputTick%60==0;
            }
            prime::benchmark_play_observe(sample);
            if(mode=="play_guard"&&control.inputTick==100)prime::benchmark_play_fail("route_guard");
        }
        ++tick;ns+=mode=="timeout"?121000000000ull:16666667ull;
        bool ready=mode!="timeout";
        if(mode=="lost_ready"&&!interrupted&&tick==400) { ready=false;interrupted=true; }
        prime::benchmark_tour_tick_at_for_testing(world,area,tick,tick,ready,ns);
    }
    assert(tick<100000);assert(!prime::benchmark_tour_enabled());
    assert(read(live/"port_settings.ini")==settings);assert(read(live/"memorycard.bin")=="PROTECTED-SAVE");
    if(mode=="complete") {
        assert(commands.size()==81); //22 inventory,48 fixed-yaw,11 cross-room loads
        for(const auto& c:commands)assert(!c.starts_with("state ")&&!c.starts_with("resume ")&&!c.starts_with("stick "));
    }
    std::printf("%s\n",(bench/"logs"/"benchmark-tour.jsonl").c_str());
    prime::benchmark_tour_reset_for_testing();
}
