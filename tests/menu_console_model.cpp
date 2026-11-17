#include "menu_console_model.hpp"
#include "Kyoto/Graphics/CCubeSurface.hpp"
#include "Kyoto/CFrameDelayedKiller.hpp"
#include "gx/fifo.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <algorithm>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"model check failed: %s line %d\n",#x,__LINE__);std::abort();}}while(false)
struct EmptyStore: IObjectStore {
  unsigned requests=0;
  CToken GetObj(const SObjectTag&,const CVParamTransfer&) override{++requests;std::abort();}
  CToken GetObj(const SObjectTag&)override{++requests;std::abort();}
  CToken GetObj(const char*)override{++requests;std::abort();}
  CToken GetObj(const char*,const CVParamTransfer&)override{++requests;std::abort();}
  bool HasObject(const SObjectTag&)const override{return false;}
  bool ObjectIsLive(const SObjectTag&)const override{return false;}
  IFactory& GetFactory()const override{std::abort();}
  void Flush()override{}
  void ObjectUnreferenced(const SObjectTag&)override{std::abort();}
};
int main(){
  EmptyStore store;auto initial=CModel::GetTotalMemory();
  for(unsigned pass=0;pass<100;++pass){
    std::unique_ptr<CModel> model(prime::menu::CreatePs4Model(store));
    CHECK(model&&model->IsLoaded(0)&&model->GetNumMaterialSets()==1);
    CHECK(model->GetCubeModel()->GetTextures().empty());
    auto surface=model->GetCubeModel()->GetNormalSurfaces();
    unsigned count=0,indices=0;
    while(surface.IsValid()){
      CHECK(surface.GetMaterialIndex()<2);
      auto dl=static_cast<const unsigned char*>(surface.GetDisplayList());
      CHECK(dl[0]==0x90);unsigned n=(unsigned(dl[1])<<8)|dl[2];
      CHECK(n%3==0 && surface.GetDisplayListSize()>=3+n*6);
      for(unsigned i=0;i<n;++i){
        auto v=dl+3+i*6;unsigned p=(unsigned(v[0])<<8)|v[1];
        CHECK(p<9019 && v[0]==v[2]&&v[1]==v[3]&&v[0]==v[4]&&v[1]==v[5]);
      }
      indices+=n;++count;surface=surface.GetNextSurface();
    }
    CHECK(count==2&&indices==6908*3);
    CHECK(!model->GetCubeModel()->GetAlphaSurfaces().IsValid());
    CHECK(std::fabs(model->GetBoundingBox().GetMinPoint().GetX()+1.375f)<1e-6f);
    CHECK(std::fabs(model->GetBoundingBox().GetMaxPoint().GetZ()-.268f)<1e-6f);
    CHECK(CModel::GetTotalMemory()>initial);
    // Exercise the actual geometry/material-vertex decode into the real GX FIFO.
    // No frame is published to a device: clear CPU commands before destroying their input.
    aurora::gx::fifo::clear_buffer();
    model->MoveToThisFrameList();
    model->GetCubeModel()->DrawFlat(nullptr,nullptr,kSS_All);
    CHECK(aurora::gx::fifo::get_buffer_size()>0);
    const auto* fifo=aurora::gx::fifo::get_buffer_data();
    const auto* end=fifo+aurora::gx::fifo::get_buffer_size();
    for(auto emitted=model->GetCubeModel()->GetNormalSurfaces();emitted.IsValid();emitted=emitted.GetNextSurface()){
      const auto* bytes=static_cast<const unsigned char*>(emitted.GetDisplayList());
      CHECK(std::search(fifo,end,bytes,bytes+emitted.GetDisplayListSize())!=end);
    }
    aurora::gx::fifo::clear_buffer();
    model.reset();CFrameDelayedKiller::FlushAllAllocations();
    CHECK(CModel::GetTotalMemory()==initial);
  }
  CHECK(store.requests==0);
  std::printf("Actual CModel loader:100 create/load/surface-walk/destroy cycles; actual DrawFlat geometry recorded;6908 triangles,2 materials,0 texture requests; deferred deletion flushed; memory returns to baseline.\n");
}
