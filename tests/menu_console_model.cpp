#include "menu_console_model.hpp"
#include "menu_console_model_data.hpp"
#include "Kyoto/Graphics/CCubeSurface.hpp"
#include "Kyoto/CFrameDelayedKiller.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include <cstring>
#include <cstdint>
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
static uint32_t be32(const unsigned char* p){return(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
static float befloat(const unsigned char* p){const auto bits=be32(p);float value;std::memcpy(&value,&bits,4);return value;}
static void check_loaded_axes_and_runtime_projection(const CModel& model){
  const auto* positions=static_cast<const unsigned char*>(model.GetCubeModel()->GetPositions());
  const auto bytes=model.GetCubeModel()->GetModelInstance().GetVertexSize();
  const auto* source=prime::menu::Ps4ModelBytes;
  const auto sectionCount=be32(source+0x24);
  const auto positionOffset=((0x2c+sectionCount*4+31)&~31u)+be32(source+0x2c);
  CHECK(std::memcmp(positions,source+positionOffset,bytes)==0); // no CMDL loader axis swizzle
  float frontLow=0,frontHigh=0,rearLow=0,rearHigh=0;
  bool fl=false,fh=false,rl=false,rh=false;
  for(unsigned i=0;i<9019;++i){
    const float y=befloat(positions+i*12+4),z=befloat(positions+i*12+8);
    const float panel=y-.6f*z;
    if(std::fabs(panel+1.3675f)<1e-5f){
      if(z<-.24f){frontLow=y;fl=true;}if(z>.24f){frontHigh=y;fh=true;}
    }
    if(std::fabs(panel-1.3675f)<1e-5f){
      if(z<-.24f){rearLow=y;rl=true;}if(z>.24f){rearHigh=y;rh=true;}
    }
  }
  CHECK(fl&&fh&&rl&&rh&&frontHigh>frontLow&&rearHigh>rearLow);
  CGraphics::SetViewport(0,0,1920,1080);CGraphics::SetPerspective(55.f,1920.f/1080.f,.2f,4096.f);
  const auto project=[](float angle,const CVector3f& point){
    const auto model=CTransform4f::RotateX(CRelAngle::FromDegrees(25.f))*
                     CTransform4f::RotateZ(CRelAngle::FromDegrees(-angle))*CTransform4f::Scale(.2f);
    const auto world=model*point;
    return CGraphics::ProjectPoint(CVector3f(world.GetX(),world.GetY()+2.f,world.GetZ()));
  };
  CHECK(project(0,CVector3f(.195f,-1.351f,0)).GetX()>960); // front USB on viewer-right
  CHECK(project(0,CVector3f(-1.14f,-1.27f,.17f)).GetX()<960); // SONY front-left
  CHECK(project(180,CVector3f(.9715f,1.441f,-.1515f)).GetX()<960); // AC rear viewer-left
  CHECK(project(180,CVector3f(.485f,1.427f,.125f)).GetX()<960); // HDMI beside AC
}
int main(){
  EmptyStore store;auto initial=CModel::GetTotalMemory();
  for(unsigned pass=0;pass<100;++pass){
    std::unique_ptr<CModel> model(prime::menu::CreatePs4Model(store));
    CHECK(model&&model->IsLoaded(0)&&model->GetNumMaterialSets()==1);
    CHECK(model->GetCubeModel()->GetTextures().empty());
    if(pass==0)check_loaded_axes_and_runtime_projection(*model);
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
  std::printf("Actual CModel loader:100 create/load/surface-walk/destroy cycles; CMDL axes and actual runtime transform/projection verified; actual DrawFlat geometry recorded;6908 triangles,2 materials,0 texture requests; deferred deletion flushed; memory returns to baseline.\n");
}
