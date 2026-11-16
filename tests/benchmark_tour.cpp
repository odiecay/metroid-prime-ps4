// SPDX-License-Identifier: MIT
#include "benchmark_tour.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
namespace fs = std::filesystem;
static std::vector<std::string> commands;
static bool rejectQueue;
static uint32_t world=0x39F2DE28, area=0x2043C96E;
bool PortConsoleRun(std::string_view text) {
    if (rejectQueue) return false;
    commands.emplace_back(text);
    if (text.starts_with("warp "))
        assert(std::sscanf(commands.back().c_str(), "warp %x %x", &world, &area)==2);
    return true;
}
extern "C" const char* prime_ps4_trace_session_id() noexcept { return "host-contract-session"; }
static std::string read(const fs::path& p) {
    std::ifstream input(p,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),{}};
}
static void write(const fs::path& p,const std::string& v) { std::ofstream(p,std::ios::binary)<<v; }
int main(int argc,char** argv) {
    assert(argc==3);const std::string mode=argv[1];const fs::path root=argv[2];
    const fs::path live=root/"live";fs::create_directories(live);
    const std::string settings="render_scale=2.25\nmsaa=1\noriginal_experience=0\n";
    write(live/"port_settings.ini",settings);write(live/"memorycard.bin","PROTECTED-SAVE");
    setenv("MP_USER_PATH",live.c_str(),1);setenv("MP_CACHE_PATH",(live/"cache").c_str(),1);
    if(mode=="absent") {
        assert(!prime::prepare_benchmark_tour(root));assert(!prime::benchmark_tour_enabled());
        assert(!fs::exists(root/"benchmarks"));assert(std::string(getenv("MP_USER_PATH"))==live);
        return 0;
    }
    write(root/"benchmark-tour.cfg",mode=="invalid"?"not-a-tour\n":"PROJECT_PRIME_BENCHMARK_TOUR_V1\n");
    if(mode=="symlink") {
        fs::create_directories(root/"benchmarks");
        fs::create_directory_symlink(live,root/"benchmarks"/"tour-v1");
    }
    if(mode=="invalid"||mode=="symlink") {
        bool failed=false;try { prime::prepare_benchmark_tour(root); }catch(...) {failed=true;}
        assert(failed);assert(!prime::benchmark_tour_enabled());
        assert(std::string(getenv("MP_USER_PATH"))==live);assert(read(live/"port_settings.ini")==settings);
        assert(read(live/"memorycard.bin")=="PROTECTED-SAVE");return 0;
    }
    assert(prime::prepare_benchmark_tour(root));
    assert(prime::benchmark_tour_enabled());const fs::path bench=root/"benchmarks"/"tour-v1";
    assert(std::string(getenv("MP_USER_PATH"))==bench/"user");
    assert(std::string(getenv("MP_CACHE_PATH"))==bench/"cache");
    assert(std::string(getenv("MP_PS4_PROFILE_LOG"))==bench/"logs"/"frame-profile.jsonl");
    assert(read(bench/"user"/"port_settings.ini")==settings);
    assert(!fs::exists(bench/"user"/"memorycard.bin"));
    if(mode=="log_failure")prime::benchmark_tour_log_failure_for_testing();
    if(mode=="queue_failure")rejectQueue=true;
    uint64_t tick=0,ns=1000000000ull;bool interrupted=false;
    while(prime::benchmark_tour_enabled()&&tick<100000) {
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
