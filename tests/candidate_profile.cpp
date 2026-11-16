// SPDX-License-Identifier: MIT
#include "../platform/ps4/candidate_profile.hpp"
#include <cassert>
#include <fstream>
#include <cstdlib>
#include <iostream>
using namespace prime;
namespace fs=std::filesystem;
void write(const fs::path& p,const char* s) { std::ofstream(p)<<s; }
int main(int argc,char** argv) {
    assert(argc==2);fs::path root=argv[1];fs::create_directories(root/"baseline/saves");
    auto desc=root/"descriptor.txt";
    write(desc,"PROJECT_PRIME_PROFILE_V1\nperformance-v0021\n");
    write(root/"baseline/saves/card.raw","original progress");
    write(root/"baseline/settings.cfg","original settings");
    auto p=read_candidate_profile(desc,root);
    prepare_candidate_storage(p,root/"baseline");
    assert(fs::file_size(p.root/"user/saves/card.raw")==17);
    write(p.root/"user/saves/card.raw","candidate progress");
    prepare_candidate_storage(p,root/"baseline");
    assert(fs::file_size(p.root/"user/saves/card.raw")==18);
    assert(fs::file_size(root/"baseline/saves/card.raw")==17);
    apply_candidate_environment(p);
    assert(fs::path(getenv("MP_USER_PATH"))==p.root/"user");
    assert(fs::path(getenv("MP_CACHE_PATH"))==p.root/"cache");
    write(p.root/"benchmark.cfg","command_storage_reuse=0\n");
    apply_candidate_environment(p);
    assert(std::string(getenv("AURORA_COMMAND_STORAGE_REUSE"))=="0");
    write(p.root/"benchmark.cfg","command_storage_reuse=1\n");
    apply_candidate_environment(p);
    assert(std::string(getenv("AURORA_COMMAND_STORAGE_REUSE"))=="1");
    unsetenv("MP_DAWN_CALL_PROFILE");
    const auto legacyBenchmark=std::string("command_storage_reuse=1\n");
    write(p.root/"benchmark-v2.cfg","dawn_call_profile=1\n");
    apply_candidate_environment(p);
    assert(std::string(getenv("MP_DAWN_CALL_PROFILE"))=="1");
    assert(std::string(getenv("AURORA_COMMAND_STORAGE_REUSE"))=="1");
    {
        std::ifstream old(p.root/"benchmark.cfg");
        assert(std::string((std::istreambuf_iterator<char>(old)),{})==legacyBenchmark);
    }
    write(p.root/"benchmark-v2.cfg","dawn_call_profile=0\n");
    apply_candidate_environment(p);
    assert(std::string(getenv("MP_DAWN_CALL_PROFILE"))=="0");
    for(const char* bad:{"dawn_call_profile=2\n","dawn_call_profile=1\ndawn_call_profile=0\n",
                        "command_storage_reuse=1\n","\n",""}) {
        write(p.root/"benchmark-v2.cfg",bad);
        bool badConfig=false;try{apply_candidate_environment(p);}catch(...){badConfig=true;}
        assert(badConfig);
        assert(std::string(getenv("MP_DAWN_CALL_PROFILE"))=="0");
    }
    fs::remove(p.root/"benchmark-v2.cfg");
    write(desc,"PROJECT_PRIME_PROFILE_V1\n../../user\n");
    bool rejected=false;try { read_candidate_profile(desc,root); } catch(...) { rejected=true; }assert(rejected);
    write(desc,"PROJECT_PRIME_PROFILE_V1\nremastered-lite-v0023\n");
    auto lite=read_candidate_profile(desc,root);assert(lite.remastered && !lite.enhanced);
    fs::create_symlink(root/"baseline/settings.cfg",root/"baseline/link");
    rejected=false;try { prepare_candidate_storage(lite,root/"baseline"); } catch(...) { rejected=true; }assert(rejected);
    assert(!fs::exists(lite.root/"user"));
    fs::remove(root/"baseline/link");
    prepare_candidate_storage(lite,root/"baseline");
    apply_candidate_environment(lite);
    assert(std::string(getenv("MP_REMASTERED_HIDE")).find("geometry")!=std::string::npos);
    assert(fs::exists(lite.root/"user/settings.cfg"));
    assert(!candidate_assets_ready(lite));
    assert(fs::path(getenv("MP_MODS"))==lite.root/"mods-fallback");
    auto mod=lite.root/"mods/remastered-models";fs::create_directories(mod);
    write(lite.root/"prime-assets.txt","PROJECT_PRIME_ASSETS_V1\nremastered-lite\nc872da704adde9a7838492aaa56dac73a32053e4\n520\n");
    write(mod/".import-version","520\nc872da704add-dirty\n");
    write(mod/"00000001.CMDL","model");
    std::ofstream index(lite.root/"prime-assets-index.txt");
    index<<fs::file_size(mod/".import-version")<<" mods/remastered-models/.import-version\n5 mods/remastered-models/00000001.CMDL\n";
    index.close();
    assert(candidate_assets_ready(lite));
    apply_candidate_environment(lite);
    assert(fs::path(getenv("MP_MODS"))==lite.root/"mods");
    write(mod/"00000001.CMDL","bad");
    assert(!candidate_assets_ready(lite));
    apply_candidate_environment(lite);
    assert(fs::path(getenv("MP_MODS"))==lite.root/"mods-fallback");
    write(mod/"00000001.CMDL","model");
    write(mod/"unlisted.PAK","unknown");assert(!candidate_assets_ready(lite));
    fs::remove(mod/"unlisted.PAK");assert(candidate_assets_ready(lite));
    assert(!fs::equivalent(lite.root/"user/saves/card.raw",p.root/"user/saves/card.raw"));
    write(desc,"PROJECT_PRIME_PROFILE_V1\nconsolidated\n");
    auto release=read_candidate_profile(desc,root);
    assert(release.root==root/"release" && release.assetsRoot==root/"assets/remastered-v1");
    prepare_candidate_storage(release,root/"baseline");
    apply_candidate_environment(release);
    assert(std::getenv("MP_REMASTERED_HIDE")==nullptr);
    assert(std::string(getenv("MP_TEXTURE_MAX_DIM"))=="0");
    assert(std::string(getenv("MP_REMASTERED_EFFECTS"))=="1");
    std::ifstream settings(release.root/"user/port_settings.ini");
    const std::string initial((std::istreambuf_iterator<char>(settings)),{});
    assert(initial.find("per_pixel_lighting=1")!=std::string::npos);
    assert(initial.find("remastered_hide=geometry,hud,font,text,map,movies")!=std::string::npos);
    const auto app=root/"app0";
    fs::create_directories(app/"remastered");
    const auto installed=read_candidate_profile(desc,root,app);
    assert(installed.assetsRoot==app/"remastered");
    assert(installed.root==release.root);
    assert(!candidate_assets_ready(installed)); // incomplete installed data is rejected
    prepare_candidate_storage(installed,root/"baseline");
    assert(fs::exists(installed.root/"user/prime-candidate-seed.txt"));
    assert(fs::file_size(root/"baseline/saves/card.raw")==17);
    write(desc,"PROJECT_PRIME_PROFILE_V1\nconsolidated-original-hd\n");
    const auto originalHd=read_candidate_profile(desc,root,app);
    assert(originalHd.root==release.root && !originalHd.remastered && originalHd.assetsRoot.empty());
    write(originalHd.root/"user/saves/card.raw","newest release progress");
    prepare_candidate_storage(originalHd,root/"baseline");
    assert(fs::file_size(originalHd.root/"user/saves/card.raw")==23);
    apply_candidate_environment(originalHd);
    assert(fs::path(getenv("MP_MODS"))==release.root/"mods");
    assert(fs::path(getenv("MP_USER_PATH"))==release.root/"user");
    assert(std::string(getenv("MP_PS4_CANDIDATE"))=="consolidated-original-hd");
    assert(candidate_assets_ready(originalHd)); // no proprietary visual set required
    assert(fs::file_size(root/"baseline/saves/card.raw")==17);
    std::cout<<"Candidate identities, independent caches/saves, no overwrite and interrupted-seed/link rejection verified\n";
}
