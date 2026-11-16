// SPDX-License-Identifier: MIT
// Host build of the same compatibility/completeness gate used by native entry.
#include "../platform/ps4/candidate_profile.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    const std::string key=argv[1];
    if(key!="remastered-lite" && key!="remastered-enhanced" && key!="remastered-consolidated") return 2;
    prime::CandidateProfile p{key=="remastered-consolidated"?"consolidated":key,argv[2],true,key!="remastered-lite"};
    const bool ready=prime::candidate_assets_ready(p);
    std::printf("Native policy profile preflight: %s; compatible, bounded, complete asset tree: %s\n",key.c_str(),ready?"PASS":"FAIL");
    return ready?0:1;
}
