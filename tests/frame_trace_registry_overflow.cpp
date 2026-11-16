#include <aurora/frame_trace.hpp>
#include <array>
#include <cstdlib>
#include <string>
extern "C" void prime_ps4_trace_start();
extern "C" void prime_ps4_trace_stop();
extern "C" { void (*prime_orbis_profile_callback)(unsigned,uint64_t)=nullptr; }
int main(int argc,char** argv){
 if(argc!=2)return 1;setenv("MP_PS4_PROFILE_LOG",argv[1],1);
 std::array<std::string,160> labels;for(unsigned i=0;i<labels.size();++i)labels[i]="overflow-"+std::to_string(i);
 prime_ps4_trace_start();
 for(auto& name:labels)aurora::trace::emit({1,1,1,name.c_str(),aurora::trace::Main,aurora::trace::Counter});
 prime_ps4_trace_stop();
}
