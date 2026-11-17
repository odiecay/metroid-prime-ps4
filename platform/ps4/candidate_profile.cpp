// SPDX-License-Identifier: MIT
#include "candidate_profile.hpp"
#include <array>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace prime {
namespace fs=std::filesystem;
CandidateProfile read_candidate_profile(const fs::path& descriptor,const fs::path& root,const fs::path& appRoot) {
    if(!fs::exists(descriptor)) return {};
    if(fs::file_size(descriptor)>128) throw std::runtime_error("Candidate profile descriptor is too large");
    std::ifstream input(descriptor);
    std::string magic,key,extra;
    if(!std::getline(input,magic) || !std::getline(input,key) || std::getline(input,extra) ||
       magic!="PROJECT_PRIME_PROFILE_V1") throw std::runtime_error("Invalid candidate profile descriptor");
    if(key=="consolidated-original-hd") {
        // The same release identity and saves, with original game resources.
        // No Remastered install, import, asset scan or unpack is required.
        return {key,root/"release",false,false};
    }
    if(key=="consolidated") {
        // Installed assets live in the read-only package filesystem. Saves,
        // configuration and caches retain their stable writable data paths.
        const auto installed=appRoot/"remastered";
        return {key,root/"release",true,true,fs::exists(installed)?installed:root/"assets/remastered-v1"};
    }
    constexpr std::array keys={"performance-v0021","original-hd-v0022","remastered-lite-v0023","remastered-enhanced-v0024"};
    bool valid=false;
    for(const auto* allowed:keys) valid|=key==allowed;
    if(!valid) throw std::runtime_error("Unsupported candidate graphics profile");
    return {key,root/"candidates"/key,key.starts_with("remastered-"),key.starts_with("remastered-enhanced")};
}
void prepare_candidate_storage(const CandidateProfile& p,const fs::path& source) {
    if(p.key.empty()) return;
    // Refuse links at every generated parent. Existing candidate saves are
    // reused; seeding never merges or overwrites files in an existing user tree.
    for(auto dir:{p.root.parent_path(),p.root,p.root/"cache",p.root/"logs",p.root/"mods"}) {
        if(fs::is_symlink(dir)) throw std::runtime_error("Candidate storage contains a symbolic link");
        fs::create_directories(dir);
    }
    const auto user=p.root/"user",temp=p.root/"user.seed-incomplete";
    if(fs::is_symlink(user) || fs::is_symlink(temp)) throw std::runtime_error("Candidate save path is a symbolic link");
    if(fs::exists(user)) {
        if(!fs::is_directory(user)) throw std::runtime_error("Candidate user path is not a directory");
        return;
    }
    fs::remove_all(temp);fs::create_directory(temp);
    uint64_t bytes=0,files=0;
    if(fs::exists(source)) {
        if(fs::is_symlink(source)) throw std::runtime_error("Baseline user path is a symbolic link");
        for(const auto& item:fs::recursive_directory_iterator(source)) {
            if(item.is_symlink()) throw std::runtime_error("Baseline user tree contains a symbolic link");
            const auto dest=temp/item.path().lexically_relative(source);
            if(item.is_directory()) fs::create_directories(dest);
            else if(item.is_regular_file()) {
                bytes+=item.file_size();++files;
                if(bytes>128ull*1024*1024 || files>4096) throw std::runtime_error("Baseline save/settings copy exceeds the candidate seed limit");
                fs::copy_file(item.path(),dest,fs::copy_options::none);
            } else throw std::runtime_error("Baseline user tree contains a non-file entry");
        }
    }
    if(p.remastered) {
        // Only this new candidate copy is configured for its explicit asset
        // experiment. Later launches preserve controller-made settings.
        std::ofstream settings(temp/"port_settings.ini",std::ios::app);
        settings<<"\n# Initial selective Remastered candidate profile\nmods=1\noriginal_experience=0\nmods_disabled=\n";
        if(p.key=="consolidated") settings<<"per_pixel_lighting=1\nrender_scale=0\ndynamic_resolution=0\nanisotropy=16\nremastered_hide=geometry,hud,font,text,map,movies\n";
        settings.close();
        if(!settings) throw std::runtime_error("Could not seed candidate graphics settings");
    }
    std::ofstream marker(temp/"prime-candidate-seed.txt");
    marker<<"PROJECT_PRIME_SEED_V1\n"<<p.key<<"\nfiles="<<files<<" bytes="<<bytes<<"\n";
    marker.close();
    if(!marker) throw std::runtime_error("Could not complete candidate save seed");
    fs::rename(temp,user);
}
bool candidate_assets_ready(const CandidateProfile& p) {
    if(!p.remastered) return true;
    try {
        const auto base=p.assetsRoot.empty()?p.root:p.assetsRoot;
        const auto marker=base/"prime-assets.txt",index=base/"prime-assets-index.txt";
        if(fs::is_symlink(marker) || fs::is_symlink(index) || !fs::is_regular_file(marker) ||
           !fs::is_regular_file(index) || fs::file_size(marker)>256 || fs::file_size(index)>2*1024*1024) return false;
        std::ifstream input(marker);std::string magic,name,pin,version,extra;
        const std::string wanted=p.key=="consolidated"?"remastered-consolidated":p.enhanced?"remastered-enhanced":"remastered-lite";
        if(!std::getline(input,magic) || !std::getline(input,name) || !std::getline(input,pin) ||
           !std::getline(input,version) || std::getline(input,extra) || magic!="PROJECT_PRIME_ASSETS_V1" || name!=wanted ||
           pin!="c872da704adde9a7838492aaa56dac73a32053e4" || version!="520") return false;
        const auto mod=base/"mods/remastered-models",stampPath=mod/".import-version";
        if(fs::is_symlink(base/"mods") || fs::is_symlink(mod) || fs::is_symlink(stampPath) ||
           !fs::is_regular_file(stampPath) || fs::file_size(stampPath)>256) return false;
        std::ifstream stamp(stampPath);
        if(!std::getline(stamp,version) || version!="520" || !std::getline(stamp,pin) || !pin.starts_with("c872da704add")) return false;
        std::ifstream catalog(index);std::string line;std::set<std::string> expected;
        uint64_t bytes=0;unsigned files=0;
        while(std::getline(catalog,line)) {
            if(line.empty() || line.size()>512 || ++files>(p.key=="consolidated"?16384u:8192u)) return false;
            std::istringstream row(line);uint64_t size=0;std::string name,junk;
            if(!(row>>size>>name) || (row>>junk) || size>64ull*1024*1024) return false;
            const fs::path rel(name);
            if(rel.is_absolute() || !name.starts_with("mods/remastered-models/") || !expected.insert(name).second) return false;
            auto path=base;
            for(const auto& part:rel) {
                if(part==".." || part==".") return false;
                path/=part;if(fs::is_symlink(path)) return false;
            }
            if(!fs::is_regular_file(path) || fs::file_size(path)!=size) return false;
            bytes+=size;
            if(bytes>(p.key=="consolidated"?3ull*1024*1024*1024:p.enhanced?2ull*1024*1024*1024:256ull*1024*1024)) return false;
        }
        if(files<2 || !expected.contains("mods/remastered-models/.import-version")) return false;
        for(const auto& item:fs::recursive_directory_iterator(base/"mods")) {
            if(item.is_symlink()) return false;
            if(item.is_regular_file() && !expected.contains(item.path().lexically_relative(base).generic_string())) return false;
        }
        return true;
    } catch(...) { return false; }
}
void apply_candidate_environment(const CandidateProfile& p) {
    if(p.key.empty()) return;
    unsetenv("MP_INSTALLED_MOD_INDEX");
    for(auto pair:{std::pair{"MP_USER_PATH","user"},std::pair{"MP_CACHE_PATH","cache"},std::pair{"MP_MODS","mods"}})
        if(setenv(pair.first,(p.root/pair.second).c_str(),1)) throw std::runtime_error("Could not set candidate storage paths");
    const auto benchmark=p.root/"benchmark.cfg";
    if(fs::exists(benchmark)) {
        if(fs::is_symlink(benchmark) || !fs::is_regular_file(benchmark) || fs::file_size(benchmark)>128)
            throw std::runtime_error("Invalid candidate benchmark configuration");
        std::ifstream config(benchmark);std::string row,extra;
        if(!std::getline(config,row) || std::getline(config,extra) ||
           (row!="command_storage_reuse=0" && row!="command_storage_reuse=1"))
            throw std::runtime_error("Benchmark configuration accepts command_storage_reuse=0 or 1 only");
        setenv("AURORA_COMMAND_STORAGE_REUSE",row.back()=='1'?"1":"0",1);
    }
    // Versioned optional diagnostics: older packages ignore this file. Never
    // put new options in benchmark.cfg, whose old parser is intentionally strict.
    const auto diagnostics=p.root/"benchmark-v2.cfg";
    if(fs::exists(diagnostics)) {
        if(fs::is_symlink(diagnostics) || !fs::is_regular_file(diagnostics) || fs::file_size(diagnostics)>128)
            throw std::runtime_error("Invalid candidate diagnostic configuration");
        std::ifstream config(diagnostics);std::string row,extra;
        if(!std::getline(config,row) || std::getline(config,extra) || config.bad() ||
           (row!="dawn_call_profile=0" && row!="dawn_call_profile=1"))
            throw std::runtime_error("Diagnostic configuration accepts dawn_call_profile=0 or 1 only");
        if(setenv("MP_DAWN_CALL_PROFILE",row.back()=='1'?"1":"0",1))
            throw std::runtime_error("Could not apply Vulkan call profiling option");
    }
    // A restart-only A/B switch for the same executable and workload. Older
    // packages ignore this versioned file; normal graphics settings stay intact.
    const auto pacing=p.root/"benchmark-v3.cfg";
    if(fs::exists(pacing)) {
        if(fs::is_symlink(pacing) || !fs::is_regular_file(pacing) || fs::file_size(pacing)>128)
            throw std::runtime_error("Invalid native presentation diagnostic configuration");
        std::ifstream config(pacing);std::string row,extra;
        if(!std::getline(config,row) || std::getline(config,extra) || config.bad() ||
           (row!="present_prewait=0" && row!="present_prewait=1"))
            throw std::runtime_error("Presentation diagnostic accepts present_prewait=0 or 1 only");
        if(setenv("MP_PS4_PRESENT_PREWAIT",row.back()=='1'?"1":"0",1))
            throw std::runtime_error("Could not apply native presentation diagnostic option");
    }
    setenv("MP_PS4_CANDIDATE",p.key.c_str(),1);
    setenv("MP_PS4_LOG_ROOT",(p.root/"logs").c_str(),1);
    const auto log=p.root/"logs/frame-profile.jsonl";
    setenv("MP_PS4_PROFILE_LOG",log.c_str(),1);
    if(p.remastered) {
        // Import switches affect conversion only. Runtime hide explicitly
        // excludes heavy effects/geometry even when the source import had them.
        const bool assets=candidate_assets_ready(p);
        const auto base=p.assetsRoot.empty()?p.root:p.assetsRoot;
        if(assets) setenv("MP_MODS",(base/"mods").c_str(),1);
        if(assets && base=="/app0/remastered")
            setenv("MP_INSTALLED_MOD_INDEX",(base/"prime-assets-index.txt").c_str(),1);
        setenv("MP_PS4_ASSETS_READY",assets?"1":"0",1);
        if(!assets) {
            const auto fallback=p.root/"mods-fallback";
            if(fs::is_symlink(fallback)) throw std::runtime_error("Candidate fallback path is a symbolic link");
            fs::create_directories(fallback);
            setenv("MP_MODS",fallback.c_str(),1);
        }
        if(p.key=="consolidated") unsetenv("MP_REMASTERED_HIDE");
        else setenv("MP_REMASTERED_HIDE","geometry,effects,hud,font,text,map,movies",1);
        setenv("MP_TEXTURE_MAX_DIM",p.key=="consolidated"?"0":p.enhanced?"2048":"1024",1);
        setenv("MP_TEXTURE_CACHE_MB",p.enhanced?"512":"256",1);
        setenv("MP_REMASTERED_GEOMETRY","none",1);
        setenv("MP_REMASTERED_EFFECTS",p.key=="consolidated"?"1":"0",1);
    }
}
}
