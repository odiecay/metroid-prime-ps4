#include "candidate_profile.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
namespace fs=std::filesystem;
static void write(const fs::path& p,const std::string& s){std::ofstream(p)<<s;}
static std::string read(const fs::path& p){std::ifstream f(p);return {(std::istreambuf_iterator<char>(f)),{}};}
int main(int argc,char** argv) {
 assert(argc==2);fs::path root=argv[1];fs::create_directories(root/"release");
 auto desc=root/"profile";write(desc,"PROJECT_PRIME_PROFILE_V1\nconsolidated-original-hd\n");
 auto profile=prime::read_candidate_profile(desc,root);
 const auto legacy=root/"release/benchmark.cfg",diagnostic=root/"release/benchmark-v2.cfg";
 write(legacy,"command_storage_reuse=1\n");
 write(root/"save","existing progress");
 const auto saved=read(root/"save"),old=read(legacy);
 unsetenv("MP_DAWN_CALL_PROFILE");
 write(diagnostic,"dawn_call_profile=1\n");prime::apply_candidate_environment(profile);
 assert(read(legacy)==old&&read(root/"save")==saved);
 assert(std::string(getenv("AURORA_COMMAND_STORAGE_REUSE"))=="1");
#ifdef BASELINE_43
 assert(getenv("MP_DAWN_CALL_PROFILE")==nullptr);
 write(diagnostic,"anything unknown to old builds\n");prime::apply_candidate_environment(profile);
 assert(getenv("MP_DAWN_CALL_PROFILE")==nullptr);
#else
 assert(std::string(getenv("MP_DAWN_CALL_PROFILE"))=="1");
 write(diagnostic,"dawn_call_profile=0\n");prime::apply_candidate_environment(profile);
 assert(std::string(getenv("MP_DAWN_CALL_PROFILE"))=="0");
 for(auto s:{"dawn_call_profile=2\n","dawn_call_profile=1\nextra=1\n","command_storage_reuse=0\n","","\n"}){
  write(diagnostic,s);bool rejected=false;try{prime::apply_candidate_environment(profile);}catch(...){rejected=true;}
  assert(rejected&&std::string(getenv("MP_DAWN_CALL_PROFILE"))=="0");
 }
 write(diagnostic,std::string(129,'a'));bool rejected=false;try{prime::apply_candidate_environment(profile);}catch(...){rejected=true;}assert(rejected);
 fs::remove(diagnostic);fs::create_symlink(root/"save",diagnostic);rejected=false;
 try{prime::apply_candidate_environment(profile);}catch(...){rejected=true;}assert(rejected);
#endif
 assert(read(legacy)==old&&read(root/"save")==saved);
}
