#include "prime_dawn_trace.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <span>
#include <vector>
#define __ORBIS__ 1
#define TRACE_EVENT_BEGIN0(...) ((void)0)
#define TRACE_EVENT_END0(...) ((void)0)
struct MaybeError{bool error=false;};
#define DAWN_TRY(expr) do{auto result=(expr);if(result.error)return result;}while(0)
using Snapshot=std::array<PrimeDawnCallStats,4>;
std::vector<Snapshot> events;
unsigned driverCalls=0,submitCalls=0;
void driver(unsigned value){driverCalls+=value;}
extern "C" void prime_ps4_dawn_duration(unsigned,uint64_t){}
extern "C" void prime_ps4_dawn_call_stats(const PrimeDawnCallStats* rows,unsigned count){assert(count==4);assert(prime::dawn_call_profile::currentBatch==nullptr);Snapshot s;std::copy_n(rows,4,s.begin());events.push_back(s);}
struct CommandRecordingContext{bool needsSubmit=false;};
struct CommandBufferBase {
 bool fail=false;
 MaybeError RecordCommands(CommandRecordingContext*) {
  PRIME_DAWN_VK_CALL(Barrier,driver,1u);
  PRIME_DAWN_VK_CALL(Pipeline,driver,1u);
  PRIME_DAWN_VK_CALL(Descriptor,driver,1u);
  for(unsigned i=0;i<100;++i)PRIME_DAWN_VK_CALL(Draw,driver,1u);
  return {fail};
 }
};
template<class T>T* ToBackend(T*p){return p;}
template<class T>using Span=std::span<T>;
class Queue {
 CommandRecordingContext context;
public:
 CommandRecordingContext* GetPendingRecordingContext(){context.needsSubmit=true;return &context;}
 MaybeError SubmitImpl(Span<CommandBufferBase*const> commands);
 MaybeError SubmitPendingCommandsImpl(){++submitCalls;return {};}
};

#include "actual_dawn_submit.inc"
int main(){
 setenv("MP_DAWN_CALL_PROFILE","1",1);
 Queue queue;CommandBufferBase a,b;CommandBufferBase* commands[]={&a,&b};
 assert(!queue.SubmitImpl(commands).error);assert(events.size()==1&&submitCalls==1&&driverCalls==206);
 assert(events[0][0].calls==200&&events[0][1].calls==2&&events[0][2].calls==2&&events[0][3].calls==2);
 b.fail=true;assert(queue.SubmitImpl(commands).error);assert(events.size()==2&&submitCalls==1&&driverCalls==412);
 assert(events[1][0].calls==200&&events[1][3].calls==2);
 assert(prime::dawn_call_profile::currentBatch==nullptr);
 b.fail=false;assert(!queue.SubmitImpl(commands).error);assert(events.size()==3&&submitCalls==2);
 assert(events[2][0].calls==200);
}
