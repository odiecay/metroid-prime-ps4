// SPDX-License-Identifier: MIT
#include "port_shadow_demand.h"
#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>
using namespace PortShadowDemand;
static void be32(std::vector<uint8_t>& data,uint32_t v) {
  for(int i=3;i>=0;--i) data.push_back(uint8_t(v>>(i*8)));
}
static std::vector<uint8_t> materials(std::initializer_list<uint32_t> flags) {
  std::vector<uint8_t> d;
  be32(d,1);be32(d,0x12345678);be32(d,flags.size());
  for(size_t i=1;i<=flags.size();++i) be32(d,i*4);
  for(auto f:flags) be32(d,f);
  return d;
}
int main() {
  assert(PbrModelLease::Count()==0);
  const auto retail=materials({0,1,1u<<7});
  const auto pbr=materials({0,1u<<14});
  assert(!MaterialSetMayHavePbr(retail.data(),retail.size()));
  auto generatedBall=retail; // Generated ball recolours/copies retail material sets.
  generatedBall[7]^=1; // Even a different TXTR id leaves the shading classification retail.
  assert(!MaterialSetMayHavePbr(generatedBall.data(),generatedBall.size()));
  assert(MaterialSetMayHavePbr(pbr.data(),pbr.size()));
  for(size_t size=0;size<retail.size();++size)
    assert(MaterialSetMayHavePbr(retail.data(),size)); // malformed input cannot suppress maps
  assert(!SunMapsNeeded(false,false,false));
  {
    PbrModelLease model;
    model.Set(MaterialSetMayHavePbr(retail.data(),retail.size()) || MaterialSetMayHavePbr(pbr.data(),pbr.size()));
    assert(PbrModelLease::Count()==1); // PBR in an alternate material set, before it is drawn.
    const bool recordedFrameSun=SunMapsNeeded(false,PbrModelLease::Count()!=0,false);
    assert(recordedFrameSun);
    {
      PbrModelLease copied(model);assert(PbrModelLease::Count()==2);
      PbrModelLease moved(std::move(copied));assert(PbrModelLease::Count()==2 && !copied.Active());
      PbrModelLease remapped;
      remapped=moved;assert(PbrModelLease::Count()==3);
      remapped=std::move(moved);assert(PbrModelLease::Count()==2 && !moved.Active());
    }
    assert(PbrModelLease::Count()==1);
    assert(SunMapsNeeded(true,false,false)); // Mod source protects a first mid-frame PBR load.
    assert(SunMapsNeeded(false,PbrModelLease::Count()!=0,false)); // Mod disabled, old model retained.
    model.Set(false);assert(PbrModelLease::Count()==0);
    assert(recordedFrameSun); // A queued frame retains its recorded decision after destruction/remap.
  }
  assert(PbrModelLease::Count()==0);
  assert(!SunMapsNeeded(false,false,false));
  assert(SunMapsNeeded(false,false,true)); // Retained icy override still needs PBR shadows.
  for(bool enabled:{false,true}) for(bool mode:{false,true}) for(bool geometry:{false,true})
    assert(ProjectedActorsNeeded(enabled,mode,geometry)==(enabled&&mode&&geometry));
  assert(!RoomReceiversReady(false,false,4,4));
  assert(!RoomReceiversReady(true,true,4,4));
  assert(!RoomReceiversReady(true,false,0,0));
  assert(!RoomReceiversReady(true,false,4,3)); // Partial load keeps original blob.
  assert(RoomReceiversReady(true,false,4,4));
  bool selected=false;
  assert(!SelectProjectedMode(selected,false));
  assert(SelectProjectedMode(selected,true) && selected); // Dirty/recalculate when geometry arrives.
  assert(!SelectProjectedMode(selected,true));
  assert(SelectProjectedMode(selected,false) && !selected); // Mod reload/room fallback restores blob.
  std::puts("shadow demand: original/generated TEV, alternate PBR sets, lease copy/move/remap/lifetime, queued decisions and projected fallback passed");
}
