#include "prime_dawn_call_profile.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
namespace p=prime::dawn_call_profile;
namespace f=p::frontend;
using Snapshot=std::array<PrimeDawnCallStats,f::CategoryCount>;
static std::vector<Snapshot> observed;
static uint64_t reads=0,clockValue=0,calls=0;
static bool invalid=false;
static uint64_t clock_fake() noexcept {++reads;return invalid?(reads&1?100:50):clockValue+=10;}
static void noop(const PrimeDawnCallStats*,unsigned){}
extern "C" void prime_ps4_dawn_frontend_stats(const PrimeDawnCallStats* data,unsigned count){
 assert(count==f::CategoryCount&&f::currentBatch==nullptr);Snapshot copy{};std::copy_n(data,count,copy.begin());observed.push_back(copy);
 f::call(f::Category::Draw,[]{++calls;}); // observer work must never recurse
}
static void early(){f::Batch b(true,prime_ps4_dawn_frontend_stats,clock_fake);f::call(f::Category::Draw,[]{++calls;});}
int main(){
 static_assert(unsigned(p::Category::Draw)==0&&unsigned(p::Category::Descriptor)==1&&unsigned(p::Category::Pipeline)==2&&unsigned(p::Category::Barrier)==3&&unsigned(p::Category::PushConstants)==4&&unsigned(p::Category::DynamicState)==5&&p::CategoryCount==6);
 for(bool enabled:{false,true}){
  const auto before=calls;reads=0;observed.clear();f::nextBatch={};
  {f::Batch batch(enabled,prime_ps4_dawn_frontend_stats,clock_fake);
   for(unsigned i=0;i<10000;++i){auto result=f::call(f::Category::Finish,[&]{++calls;return std::make_unique<unsigned>(i);});assert(result&&*result==i);}}
  assert(calls==before+10000+(enabled?1:0));assert(enabled?reads==128:reads==0);assert(observed.size()==unsigned(enabled));
 }
 observed.clear();f::nextBatch={};reads=0;
 for(unsigned b=0;b<64;++b){f::Batch batch(true,prime_ps4_dawn_frontend_stats,clock_fake);for(unsigned c=0;c<f::CategoryCount;++c)f::call(f::Category(c),[]{++calls;});}
 assert(observed.size()==64&&reads==2*f::CategoryCount);
 for(unsigned c=0;c<f::CategoryCount;++c){uint64_t samples=0;for(const auto& row:observed){assert(row[c].calls==1);samples+=row[c].samples;}assert(samples==1);}
 observed.clear();f::nextBatch={};reads=0;
 for(unsigned frame=0;frame<64;++frame){
  for(unsigned j=0;j<4;++j){f::Batch encode(true,noop,clock_fake);f::call(f::Category::Draw,[]{++calls;});}
  {f::Batch present(true,prime_ps4_dawn_frontend_stats,clock_fake,f::BatchKind::Present);f::call(f::Category::Pipeline,[]{++calls;});}
  {f::Batch finish(true,prime_ps4_dawn_frontend_stats,clock_fake,f::BatchKind::Finish);f::call(f::Category::Finish,[]{++calls;});}
 }
 uint64_t finishSamples=0,presentSamples=0;
 for(const auto& row:observed){finishSamples+=row[unsigned(f::Category::Finish)].samples;presentSamples+=row[unsigned(f::Category::Pipeline)].samples;}
 assert(finishSamples==1&&presentSamples==1);
 observed.clear();f::nextBatch={};reads=0;
 {f::Batch batch(true,prime_ps4_dawn_frontend_stats,clock_fake);for(unsigned i=0;i<100000;++i)for(unsigned c=0;c<f::CategoryCount;++c)f::call(f::Category(c),[]{++calls;});}
 for(const auto& s:observed.back())assert(s.calls==100000&&s.samples==64&&s.sampledWallNs==640&&s.cappedSamples>0);
 assert(reads==2*64*f::CategoryCount);
 observed.clear();f::nextBatch={};
 {p::Batch driver(true,noop,clock_fake);auto* driverScope=p::currentBatch;
  {f::Batch outer(true,prime_ps4_dawn_frontend_stats,clock_fake);f::call(f::Category::Pipeline,[]{++calls;});
   {f::Batch paused(false,prime_ps4_dawn_frontend_stats,clock_fake);f::call(f::Category::Draw,[]{++calls;});}
   {f::Batch inner(true,prime_ps4_dawn_frontend_stats,clock_fake);f::call(f::Category::BindGroup,[]{++calls;});}
   assert(p::currentBatch==driverScope);}
  assert(p::currentBatch==driverScope&&f::currentBatch==nullptr);}
 assert(p::currentBatch==nullptr&&observed.size()==2&&observed[0][1].calls==1&&observed[1][0].calls==1);
 observed.clear();f::nextBatch={};reads=0;invalid=true;
 {f::Batch batch(true,prime_ps4_dawn_frontend_stats,clock_fake);f::call(f::Category::Pipeline,[]{++calls;});}
 assert(observed.back()[0].samples==0&&observed.back()[0].invalidClocks==1);invalid=false;
 observed.clear();early();assert(observed.size()==1&&f::currentBatch==nullptr);
 reads=0;{f::Batch missing(true,nullptr,clock_fake);f::call(f::Category::Draw,[]{++calls;});}assert(reads==0&&f::currentBatch==nullptr);
 std::puts("frontend_families=6 actual_return_values=10000 sample_cap=64 rotation=64 driver_ids_preserved=1 independent_nested_tls=1 early_error_restore=1 zero_disabled_clocks=1");
}
