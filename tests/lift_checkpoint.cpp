#include "port_savestate.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
int prime_lift_checkpoint_main(int, char**);
namespace fs = std::filesystem;
std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const fs::path dir(argv[1]), source=dir/"resume.mpss", target=dir/"test.mpss";
  PortSaveState::Header h;
  h.worldId=0x158efe17u; h.areaId=5; h.disc="USA 1.00";
  h.position[2]=-33000.f;
  h.roomState.areas.push_back(5);
  PortSaveState::RoomObject platform;
  platform.editorId=0x00050009u;
  PortSaveState::BlockWriter transform;
  const float xf[12]={1,0,0,-8.848f,0,1,0,68.771f,0,0,1,5.334f};
  for(float v:xf) transform.F32(v);
  platform.blocks.push_back({1,transform.bytes});
  h.roomState.objects.push_back(platform);
  PortSaveState::ResumeMeta meta;
  meta.cardSerial=42;
  std::vector<uint8_t> blob(4096);
  for (size_t i=0;i<blob.size();++i) blob[i]=uint8_t(i);
  auto write=[&] {
    std::ofstream out(source,std::ios::binary);
    const auto s=PortSaveState::EncodeResume(meta,PortSaveState::Encode(h,blob));
    out.write(s.data(),s.size());
  };
  auto run=[&] {
    std::string a=source.string(),b=target.string(),name="prepare";
    char* args[]={name.data(),a.data(),b.data()};
    return prime_lift_checkpoint_main(3,args);
  };
  write();const auto original=Read(source);
  assert(run()==0 && Read(source)==original);
  const auto output=Read(target);
  assert(run()==6 && Read(target)==output);
  PortSaveState::Header check;
  PortSaveState::ResumeMeta checkMeta;
  std::string state; std::vector<uint8_t> checkBlob;
  assert(PortSaveState::DecodeResume(output,checkMeta,state));
  assert(PortSaveState::Decode(state,check,checkBlob));
  assert(checkBlob==blob && check.areaId==5 && checkMeta.cardSerial==42 && check.roomState.empty());
  fs::remove(target);
  {
    std::string a=source.string(),b=target.string(),name="prepare",option="--new-console-slot3";
    char* args[]={name.data(),a.data(),b.data(),option.data()};
    assert(prime_lift_checkpoint_main(4,args)==0);
    assert(PortSaveState::DecodeResume(Read(target),checkMeta,state) && checkMeta.fileIdx==2);
    assert(PortSaveState::Decode(state,check,checkBlob) && checkBlob==blob);
    assert(Read(source)==original);
    fs::remove(target);
  }
  {
    std::string a=source.string(),b=target.string(),name="prepare",option="--mech-entrance-slot2";
    char* args[]={name.data(),a.data(),b.data(),option.data()};
    assert(prime_lift_checkpoint_main(4,args)==0);
    assert(PortSaveState::DecodeResume(Read(target),checkMeta,state) && checkMeta.fileIdx==1);
    assert(PortSaveState::Decode(state,check,checkBlob) && checkBlob==blob);
    assert(check.room=="Deck Alpha Mech Shaft entrance" && check.roomState.objects.size()==1);
    assert(check.position[2]>6.8f && check.position[2]<6.9f && check.forward[1]==-1.f);
    assert(Read(source)==original);
    fs::remove(target);
  }
  {
    std::string a=source.string(),b=target.string(),name="prepare",option="--before-lift-room-slot2";
    char* args[]={name.data(),a.data(),b.data(),option.data()};
    assert(prime_lift_checkpoint_main(4,args)==0);
    assert(PortSaveState::DecodeResume(Read(target),checkMeta,state) && checkMeta.fileIdx==1);
    assert(PortSaveState::Decode(state,check,checkBlob) && checkBlob==blob);
    assert(check.areaId==7 && check.room=="Lift room approach" && check.roomState.empty());
    assert(check.position[0]>59.60524f && check.forward[0]==-1.f);
    assert(Read(source)==original);fs::remove(target);
  }
  h.worldId=0;write();assert(run()==4 && !fs::exists(target));h.worldId=0x158efe17u;
  h.areaId=3;write();assert(run()==4);h.areaId=5;
  h.disc="PAL";write();assert(run()==4);h.disc="USA 1.00";
  meta.identity="rando:test";write();assert(run()==4);meta.identity.clear();
  meta.cardSerial=0;write();assert(run()==4);meta.cardSerial=42;
  {std::ofstream corrupt(source);corrupt<<"truncated";}
  assert(run()==4 && !fs::exists(target));
}
