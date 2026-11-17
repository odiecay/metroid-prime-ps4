// SPDX-License-Identifier: MIT
#include <aurora/frame_trace.hpp>
#include "../platform/ps4/compat/prime_dawn_call_profile.h"
#include <cstdlib>
#include <thread>
#include <array>
#include <string>
extern "C" {
void (*prime_orbis_profile_callback)(unsigned,uint64_t)=nullptr;
void prime_ps4_trace_start();
void prime_ps4_trace_stop();
void prime_ps4_dawn_duration(unsigned,uint64_t);
void prime_ps4_descriptor_bind_counts(uint64_t,uint64_t);
void prime_ps4_bind_group_usage_counts(uint64_t,uint64_t);
}
int main(int argc,char** argv) {
    if(argc!=2) return 1;
    setenv("MP_PS4_PROFILE_LOG",argv[1],1);
    setenv("MP_DAWN_CALL_PROFILE","1",1);
    prime_ps4_trace_start();
    using namespace aurora::trace;
    for(uint64_t id=1;id<=2;++id) {
        emit({id,id*100000000,id*10000000,"simulation",Main,Phase});
        emit({id,id*100000000,id*10000000,"complete frame wall",Main,Complete});
    }
    // More than 512 completed frames in one diagnostic window must not wrap
    // the bounded bucket table and discard earlier samples.
    for(uint64_t id=3;id<=602;++id) {
        emit({id,id*100000000,1000,"burst work",Main,Phase});
        emit({id,id*100000000,2000,"complete frame wall",Main,Complete});
    }
    // Real game paths accumulate distinct stages throughout a process. The
    // previous 80-stage registry overflowed once v30 reached heavier rooms.
    static const auto registryLabels=[] {
        std::array<std::string,96> labels;
        for(unsigned i=0;i<labels.size();++i) labels[i]="registry-stage-"+std::to_string(i);
        return labels;
    }();
    for(const auto& label:registryLabels)
        emit({603,60300000000ull,1,label.c_str(),Main,Phase});
    emit({603,60300000000ull,2000,"complete frame wall",Main,Complete});
    std::thread render([] {
        using namespace aurora::trace;
        for(uint64_t id=1;id<=2;++id) {
            contexts[Render].frame=id;
            prime_ps4_dawn_duration(0,1000000);
            prime_ps4_dawn_duration(1,200000);
            prime_ps4_dawn_duration(2,500000);
            prime_ps4_dawn_duration(3,300000);
            prime_ps4_dawn_duration(4,1500000);
            prime_ps4_dawn_duration(5,100000);
            const PrimeDawnCallStats calls[]={
                {128,2,1000,0},{64,1,200,0},{1,0,0,0},{32,1,50,id==2?1ull:0ull},
                {64,1,80,0,0},{128,2,100,0,3}};
            prime_ps4_dawn_call_stats(calls,6);
            const PrimeDawnCallStats frontend[]={
                {128,2,1000,0,0},{64,1,200,0,0},{128,2,300,0,0},
                {128,2,500,0,0},{128,2,600,0,0},{1,0,0,id==2?1ull:0ull,id==2?3ull:0ull}};
            prime_ps4_dawn_frontend_stats(frontend,6);
            prime_ps4_descriptor_bind_counts(2,3);
            prime_ps4_bind_group_usage_counts(5,2);
            emit({id,id*100000000,3000000,"Dawn encoding (inclusive)",Render,Inclusive});
            emit({id,id*100000000,id==2?30000000ull:5000000ull,"complete frame wall",Render,Complete});
        }
    });render.join();
    prime_ps4_trace_stop();
}
