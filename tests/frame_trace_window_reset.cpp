#include <aurora/frame_trace.hpp>
#include <cstdlib>
#include <cstdint>
#include "../platform/ps4/frame_trace_writer.cpp"
extern "C" { void (*prime_orbis_profile_callback)(unsigned,uint64_t)=nullptr; }
int main(int argc,char** argv) {
 if(argc!=2)return 1;
 setenv("MP_PS4_PROFILE_LOG",argv[1],1);
 using namespace aurora::trace;
 emit({1,1,1000000,"old-window",Main,Phase});emit({1,1,1000000,"complete frame wall",Main,Complete});
 emit({2,2,20000000,"old-window",Main,Phase});emit({2,2,20000000,"complete frame wall",Main,Complete});
 emit({99,2,70000000,"pending-span",Main,Phase});
 count("window-counter",7,Main);
 poll_trace(1);poll_trace(5000000002ull);
 emit({99,3,80000000,"complete frame wall",Main,Complete});
 emit({100,4,5000000,"fresh-span",Main,Phase});emit({100,4,5000000,"complete frame wall",Main,Complete});
 count("window-counter",3,Main);
 poll_trace(10000000003ull);poll_trace(15000000004ull);
 return 0;
}
