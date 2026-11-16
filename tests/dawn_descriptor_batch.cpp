
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <vector>
#define DAWN_ASSERT(x) assert(x)
constexpr uint32_t kMaxBindGroups=4;
constexpr uint32_t kMaxDynamicUniformBuffersPerPipelineLayout=16;
constexpr uint32_t kMaxDynamicStorageBuffersPerPipelineLayout=16;
using BindGroupIndex=uint32_t;
struct VkDescriptorSet {
 uint64_t handle=0;
 uint64_t& operator*(){return handle;}
 bool operator==(const VkDescriptorSet&) const=default;
};
static_assert(sizeof(VkDescriptorSet)==sizeof(uint64_t));
const uint64_t* AsVkArray(const VkDescriptorSet* p){return reinterpret_cast<const uint64_t*>(p);}
struct Layout {
 std::array<unsigned,6> sets{};
 unsigned push=0;
 bool operator==(const Layout&)const=default;
};
std::vector<Layout> layouts;
bool compatible(unsigned a,unsigned b,unsigned set) {
 if(layouts[a].push!=layouts[b].push)return false;
 for(unsigned i=0;i<=set;++i)if(layouts[a].sets[i]!=layouts[b].sets[i])return false;
 return true;
}
struct Binding {
 uint64_t handle=0;
 unsigned layout=0;
 bool valid=false;
 std::vector<uint32_t> offsets;
 bool operator==(const Binding&)const=default;
};
struct Call {
 unsigned layout,first;
 std::vector<uint64_t> sets;
 std::vector<uint32_t> offsets;
};
struct Driver {
 std::vector<Call> calls;
 std::array<unsigned,64> dynamics{};
 const uint32_t* lastOffsets=nullptr;
 void CmdBindDescriptorSets(unsigned command,unsigned bindPoint,unsigned layout,unsigned first,
                            unsigned count,const uint64_t* sets,unsigned offsetCount,const uint32_t* offsets) {
  assert(command==11 && bindPoint==2 && count && first+count<=6);
  assert((offsetCount==0)==(offsets==nullptr));
  lastOffsets=offsets;
  unsigned needed=0;for(unsigned i=0;i<count;++i){assert(sets[i]<dynamics.size());needed+=dynamics[sets[i]];}
  assert(needed==offsetCount);
  Call call{layout,first,{sets,sets+count},{}};
  if(offsetCount)call.offsets.assign(offsets,offsets+offsetCount);
  calls.push_back(std::move(call));
 }
 std::vector<Binding> flattened()const {
  std::vector<Binding> out;
  for(const auto&call:calls) {
   unsigned at=0;
   for(unsigned i=0;i<call.sets.size();++i) {
    unsigned n=dynamics[call.sets[i]];
    out.push_back({(uint64_t(call.first+i)<<32)|call.sets[i],call.layout,true,
                   {call.offsets.begin()+at,call.offsets.begin()+at+n}});
    at+=n;
   }
   assert(at==call.offsets.size());
  }
  return out;
 }
 std::array<Binding,6> apply(std::array<Binding,6> state)const {
  // Vulkan set compatibility includes the push constant ranges and layouts0..N.
  // Each run binds sets in increasing order with one common layout. Disturbed
  // sets keep that layout while their contents become invalid.
  for(const auto&call:calls) {
   unsigned at=0;
   for(unsigned i=0;i<call.sets.size();++i) {
    const unsigned n=call.first+i;
    for(unsigned m=0;m<n;++m)if(!compatible(state[m].layout,call.layout,m))
      state[m]={0,call.layout,false,{}};
    if(!compatible(state[n].layout,call.layout,n))
      for(unsigned m=n+1;m<state.size();++m)state[m]={0,call.layout,false,{}};
    const auto count=dynamics[call.sets[i]];
    state[n]={call.sets[i],call.layout,true,{call.offsets.begin()+at,call.offsets.begin()+at+count}};
    at+=count;
   }
  }
  return state;
 }
};
struct Fixture {
 Driver vk;
 uint64_t trackerCalls=0,trackerSets=0;
 void NoteDescriptorBind(uint32_t sets){++trackerCalls;trackerSets+=sets;}
 unsigned commandBuffer=11,bindPoint=2,layout=0,startOfBindGroups=0;
 std::vector<BindGroupIndex> dirtyBindGroups;
 std::array<VkDescriptorSet,4> groups{};
 std::array<std::vector<uint32_t>,4> offsets;
 VkDescriptorSet GetDescriptorSet(unsigned i){return groups[i];}
 std::span<const uint32_t> GetDynamicOffsets(unsigned i){return offsets[i];}
 void original() {
        for (BindGroupIndex dirtyBGIndex : dirtyBindGroups) {
            VkDescriptorSet set = GetDescriptorSet(dirtyBGIndex);
            uint32_t setIndex = uint32_t{dirtyBGIndex + startOfBindGroups};

            const auto dynamicOffsetSpan = GetDynamicOffsets(dirtyBGIndex);
            uint32_t count = static_cast<uint32_t>(dynamicOffsetSpan.size());
            const uint32_t* dynamicOffset = count > 0 ? dynamicOffsetSpan.data() : nullptr;

            vk.CmdBindDescriptorSets(commandBuffer, bindPoint, layout, setIndex, 1, &*set, count,
                                     dynamicOffset);
        }

 }
 void batched() {
#include "actual_descriptor_batch.inc"

 }
};
int main() {
 layouts.push_back({{1,2,3,4,5,6},0});
 for(unsigned prefix=0;prefix<6;++prefix) {
  auto l=layouts[0];l.sets[prefix]+=10;layouts.push_back(l);
 }
 auto push=layouts[0];push.push=1;layouts.push_back(push);
 std::mt19937 rng(0x192060);
 uint64_t cases=0,oldCalls=0,newCalls=0;
 // Exhaust every dirtymask, every0/1/maximum dynamic-offsetcount combination,
 // and prefix shifts for framebufferfetch/resource tables.
 for(unsigned mask=0;mask<16;++mask)for(unsigned prefix=0;prefix<3;++prefix)
 for(unsigned encoded=0;encoded<81;++encoded) {
  Fixture f;f.startOfBindGroups=prefix;f.layout=rng()%layouts.size();
  unsigned pattern=encoded;
  for(unsigned i=0;i<4;++i) {
   unsigned n=std::array{0u,1u,32u}[pattern%3];pattern/=3;
   f.groups[i].handle=i+1;f.vk.dynamics[i+1]=n;
   if(mask&(1<<i))f.dirtyBindGroups.push_back(i);
   for(unsigned j=0;j<n;++j)f.offsets[i].push_back((rng()&0xfffffffc));
  }
  auto b=f;f.original();b.batched();
  assert(f.vk.flattened()==b.vk.flattened());
  assert(b.trackerCalls==b.vk.calls.size() && b.trackerSets==b.vk.flattened().size());
  unsigned runs=0;for(unsigned i=0;i<4;++i)if((mask&(1<<i))&&(i==0||!(mask&(1<<(i-1)))))++runs;
  assert(b.vk.calls.size()==runs);
  std::array<Binding,6> prior;
  for(auto&x:prior)x={uint64_t(16+rng()%32),unsigned(rng()%layouts.size()),bool(rng()%2),{uint32_t(rng())}};
  assert(f.vk.apply(prior)==b.vk.apply(prior));
  oldCalls+=f.vk.calls.size();newCalls+=b.vk.calls.size();++cases;
 }
 // Random sequence of layout changes, arbitrary pre-existing bindings and gaps.
 std::array<Binding,6> oldState,newState;
 for(unsigned step=0;step<100000;++step) {
  Fixture f;f.layout=rng()%layouts.size();f.startOfBindGroups=rng()%3;
  for(unsigned i=0;i<4;++i) {
   f.groups[i].handle=1+rng()%60;
   unsigned n=rng()%13;
   // A handle has one fixed descriptor layout/count within a submission.
   f.vk.dynamics[f.groups[i].handle]=n;
   f.offsets[i].resize(n);for(auto&v:f.offsets[i])v=rng()&0xfffffffc;
   if(rng()%2)f.dirtyBindGroups.push_back(i);
  }
  // Distinct handles here avoid an artificial conflicting layout for aliases.
  for(unsigned i=0;i<4;++i){f.groups[i].handle=i+1;f.vk.dynamics[i+1]=f.offsets[i].size();}
  auto b=f;f.original();b.batched();
  assert(f.vk.flattened()==b.vk.flattened());
  assert(b.trackerCalls==b.vk.calls.size() && b.trackerSets==b.vk.flattened().size());
  oldState=f.vk.apply(oldState);newState=b.vk.apply(newState);
  assert(oldState==newState);
  ++cases;
 }
 // Typical GX: updated uniform offset in set1 and a new texture in set2.
 Fixture gx;gx.dirtyBindGroups={1,2};gx.groups[1].handle=2;gx.groups[2].handle=3;
 gx.offsets[1]={256};gx.vk.dynamics[2]=1;
 auto b=gx;gx.original();b.batched();
 assert(gx.vk.calls.size()==2 && b.vk.calls.size()==1 && gx.vk.flattened()==b.vk.flattened());
 assert(b.vk.lastOffsets==b.offsets[1].data()); // one dynamic group uses its stable tracker span
 printf("cases=%llu exhaustive_original_calls=%llu exhaustive_batched_calls=%llu gx_calls=2->1; bindings/dynamicoffsets/layoutdisturbance equivalent\n",
        (unsigned long long)cases,(unsigned long long)oldCalls,(unsigned long long)newCalls);
}
