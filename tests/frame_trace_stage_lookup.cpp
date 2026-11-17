// SPDX-License-Identifier: MIT
#include <aurora/frame_trace.hpp>
#include <cassert>
#include <cstdio>
#include <memory>
#include <map>
#include <string>
#include <tuple>
#include "../platform/ps4/frame_trace_writer.cpp"
extern "C" { void (*prime_orbis_profile_callback)(unsigned,uint64_t)=nullptr; }
int main(){
 using namespace aurora::trace;
 std::map<std::tuple<std::string,Kind,Lane>,unsigned> expected;
 char scratch[64]{};
 for(unsigned n=0;n<30000;++n){
  std::snprintf(scratch,sizeof(scratch),"mutable-label-%u",(n*37u)%12u);
  const auto kind=n%3?Counter:Phase;const auto lane=n%5?Main:Render;
  auto key=std::tuple{std::string(scratch),kind,lane};Event e{0,0,1,scratch,lane,kind};
  const unsigned index=stage_index(e);assert(index<MaxStages);
  auto[it,added]=expected.emplace(key,index);assert(added||it->second==index);
  assert(!std::strcmp(stages[index].name.data(),scratch));
 }
 auto owned=std::make_unique<char[]>(32);std::strcpy(owned.get(),"retired-label-source");
 unsigned held=stage_index({0,0,1,owned.get(),Main,Counter});owned.reset();
 assert(!std::strcmp(stages[held].name.data(),"retired-label-source"));
 char aliasA[]="alias";char aliasB[]="alias";
 const unsigned a=stage_index({0,0,1,aliasA,Main,Counter});assert(stage_index({0,0,1,aliasB,Main,Counter})==a);
 for(auto& slot:stageLookups)slot={reinterpret_cast<uintptr_t>(aliasA),held};
 assert(stage_index({0,0,1,aliasA,Main,Counter})==a);
 char maximalLabel[256];std::memset(maximalLabel,'y',255);maximalLabel[255]=0;
 assert(stage_index({0,0,1,maximalLabel,Main,Counter})<MaxStages);
 const auto unknown=unknownStages;assert(stage_index({0,0,1,nullptr,Main,Counter})==MaxStages);
 char longLabel[257];std::memset(longLabel,'x',256);longLabel[256]=0;
 assert(stage_index({0,0,1,longLabel,Main,Counter})==MaxStages);assert(unknownStages==unknown+2);
 std::array<std::array<char,32>,MaxStages> tail{};
 for(unsigned n=0;stageCount<MaxStages;++n){std::snprintf(tail[n].data(),tail[n].size(),"fill-%u",n);assert(stage_index({0,0,1,tail[n].data(),Main,Counter})<MaxStages);}
 assert(stage_index({0,0,1,"overflow-stage",Main,Counter})==MaxStages);
 std::printf("label_queries=30000 pointer_reuse=1 text_alias=1 stale_cache_collision=1 source_lifetime=1 bounded_name_and_registry=1 table_bytes=%zu lookup_bytes=%zu\n",sizeof(stages),sizeof(stageLookups));
}
