#include "port_input_devices.h"
#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
using namespace PortInput;
namespace PortDebug {bool PreciseStickDeadZones(){return false;}}
namespace PortRadial {bool Capturing(){return false;}}
namespace PortInputDevices {
static std::mutex sPromptMutex;
static std::vector<Binding>sPromptBindings;
static uint16_t contexts=kCtxMap;
uint16_t GameContexts(){return contexts;}
u32 PromptNativeButton(const Input&){return PAD_NATIVE_BUTTON_INVALID;}
#include "map_functions.inc"
}
const char* SuffixForSdlButton(int){return nullptr;}
#include "map_glyph.inc"
using namespace PortInputDevices;

Profile standard(){
  Profile p;
  const Action actions[]={Action::MainLeft,Action::MainRight,Action::MainUp,Action::MainDown,
    Action::CLeft,Action::CRight,Action::CUp,Action::CDown};
  const int axes[]={0,0,1,1,2,2,3,3},signs[]={-1,1,-1,1,-1,1,-1,1};
  for(unsigned i=0;i<8;++i)BindIn(p,actions[i],Make(Device::PadAxis,axes[i],signs[i]),kCtxAll);
  BindIn(p,Action::PadA,Make(Device::PadButton,SDL_GAMEPAD_BUTTON_SOUTH),kCtxAll);
  BindIn(p,Action::PadB,Make(Device::PadButton,SDL_GAMEPAD_BUTTON_EAST),kCtxAll);
  BindIn(p,Action::PadZ,Make(Device::PadButton,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),kCtxAll);
  BindIn(p,Action::LAnalog,Make(Device::PadAxis,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,1),kCtxAll);
  BindIn(p,Action::RAnalog,Make(Device::PadAxis,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,1),kCtxAll);
  return p;
}
unsigned count_rows(const Profile& p){unsigned count=0;for(const auto& b:p.bindings)if(FindMapRow(b.action)&&b.count)++count;return count;}
Profile active(const PortInput::UserBindings& user){
  Profile p=standard();BindMapDefaults(p);
  const auto selected=Select(user,"00112233445566778899aabbccddeeff","ps4");
  if(selected.base)Overlay(p,*selected.base);
  if(selected.pad)Overlay(p,*selected.pad);
  BindMapDefaults(p);ApplyMapRows(p);return p;
}
void verify_glyphs(const Profile& p){
  UpdatePromptBindings(p);contexts=kCtxMap;Input in;
  const PADAxis rotate[]={PAD_AXIS_LEFT_Y_POS,PAD_AXIS_LEFT_Y_NEG,PAD_AXIS_LEFT_X_NEG,PAD_AXIS_LEFT_X_POS};
  const PADAxis pan[]={PAD_AXIS_RIGHT_Y_POS,PAD_AXIS_RIGHT_Y_NEG,PAD_AXIS_RIGHT_X_NEG,PAD_AXIS_RIGHT_X_POS};
  const char* suffix[]={"_up","_down","_left","_right"};
  for(unsigned i=0;i<4;++i){
#ifdef __ORBIS__
    assert(PadStemForAxis(rotate[i],"playstation",false)==std::string("playstation_stick_r")+suffix[i]);
    assert(PadStemForAxis(pan[i],"playstation",false)==std::string("playstation_stick_l")+suffix[i]);
#else
    assert(PadStemForAxis(rotate[i],"playstation",false)==std::string("playstation_stick_l")+suffix[i]);
    assert(PadStemForAxis(pan[i],"playstation",false)==std::string("playstation_stick_r")+suffix[i]);
#endif
  }
  assert(PromptAxisInput(PAD_AXIS_LEFT_X_POS,Family::Pad,in));
#ifdef __ORBIS__
  assert(in.code==SDL_GAMEPAD_AXIS_RIGHTX);
  assert(PadStemForAxis(PAD_AXIS_LEFT_X_POS,"playstation",true)=="playstation_stick_r");
  assert(PadStemForAxis(PAD_AXIS_RIGHT_X_POS,"playstation",true)=="playstation_stick_l");
  assert(PadStemForAxis(PAD_AXIS_LEFT_Y_POS,"playstation",false)=="playstation_stick_r_up");
  assert(PadStemForAxis(PAD_AXIS_RIGHT_Y_NEG,"playstation",false)=="playstation_stick_l_down");
#else
  assert(in.code==SDL_GAMEPAD_AXIS_LEFTX);
  assert(PadStemForAxis(PAD_AXIS_LEFT_X_POS,"playstation",true)=="playstation_stick_l");
  assert(PadStemForAxis(PAD_AXIS_RIGHT_X_POS,"playstation",true)=="playstation_stick_r");
#endif
  contexts=kCtxGameplay;
  assert(PadStemForAxis(PAD_AXIS_LEFT_X_POS,"playstation",true)=="playstation_stick_l");
  assert(PadStemForAxis(PAD_AXIS_RIGHT_X_POS,"playstation",true)=="playstation_stick_r");
}
int main(){
  Profile p=active({});
#ifdef __ORBIS__
  assert(count_rows(p)==8);
#else
  assert(count_rows(p)==0);
#endif
  const auto frozen=p;BindMapDefaults(p);assert(p==frozen);verify_glyphs(p);
  unsigned samples=0;
  for(uint16_t ctx:{kCtxGameplay,kCtxMap,kCtxMenu})for(int axis=0;axis<4;++axis)for(float value:{-1.f,-.5f,.5f,1.f}){
    Runtime runtime;runtime.SetProfile(&p);RawState raw;runtime.Poll(raw,ctx,0);raw.padAxes[axis]=value;
    const auto& result=runtime.Poll(raw,ctx,1);const auto pad=Synthesize(result);
    int target=axis;
#ifdef __ORBIS__
    if(ctx==kCtxMap)target=(axis+2)%4;
#endif
    const int expected=target%2?StickY(value,0):StickX(value,0);
    const int axes[]={pad.stickX,pad.stickY,pad.substickX,pad.substickY};
    // Retain the existing PAD wire conversion, including its neutral Y byte;
    // the game's later PADClamp removes values inside its centre deadzone.
    for(unsigned i=0;i<4;++i)assert(axes[i]==(int(i)==target?expected:(i%2?StickY(0,0):0)));
    ++samples;
  }
  // Diagonals, simultaneous sticks, zoom and confirm/cancel/map remain separate.
  for(uint16_t ctx:{kCtxGameplay,kCtxMap,kCtxMenu})for(int n=0;n<34;++n){
    Runtime runtime;runtime.SetProfile(&p);RawState raw;runtime.Poll(raw,ctx,0);
    raw.padAxes[0]=.5f;raw.padAxes[1]=-.75f;raw.padAxes[2]=-.25f;raw.padAxes[3]=1.f;
    raw.padAxes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]=.75f;raw.padAxes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]=.5f;
    raw.padButtons=(1u<<SDL_GAMEPAD_BUTTON_SOUTH)|(1u<<SDL_GAMEPAD_BUTTON_EAST)|(1u<<SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    const auto pad=Synthesize(runtime.Poll(raw,ctx,1));
    const bool swap=
#ifdef __ORBIS__
      ctx==kCtxMap;
#else
      false;
#endif
    assert(pad.stickX==StickX(swap?-.25f:.5f,0));assert(pad.substickX==StickX(swap?.5f:-.25f,0));
    assert(pad.stickY==StickY(swap?1.f:-.75f,0));assert(pad.substickY==StickY(swap?-.75f:1.f,0));
    assert(pad.triggerLeft==PortInputDevices::Trigger(.75f,false)&&pad.triggerRight==PortInputDevices::Trigger(.5f,false));
    assert((pad.button&(PAD_BUTTON_A|PAD_BUTTON_B|PAD_TRIGGER_Z))==(PAD_BUTTON_A|PAD_BUTTON_B|PAD_TRIGGER_Z));++samples;
  }
  assert(samples==150);
  const char* saved[]={"", "[[profile]]\nname=\"Default\"\n",
    "[[profile]]\nname=\"Custom old name\"\nreplace=true\n",
    "[[profile]]\nname=\"Default\"\nunbind=[\"map_rotate_up@pad\",\"map_pan_down@pad\"]\n"};
  for(const char* text:saved){PortInput::UserBindings user;std::string error;assert(ParseUserBindings(text,user,&error));const auto saved=user;
    const auto actual=active(user);assert(user==saved);
#ifdef __ORBIS__
    assert(count_rows(actual)==8);
#endif
  }
  // A selected custom-controller GUID profile with real map rows wins unchanged.
  PortInput::UserBindings user;std::string error;
  assert(ParseUserBindings("[[profile]]\nname=\"Mine\"\nmatch=\"guid:00112233445566778899aabbccddeeff\"\n[[profile.bind]]\naction=\"map_rotate_up\"\ninputs=[\"pad:dpup\"]\n",user,&error));
  const auto savedUser=user;auto custom=active(user);assert(user==savedUser);
  UpdatePromptBindings(custom);contexts=kCtxMap;Input actual;
  assert(PromptAxisInput(PAD_AXIS_LEFT_Y_POS,Family::Pad,actual));
  assert(actual.device==Device::PadButton&&actual.code==SDL_GAMEPAD_BUTTON_DPAD_UP);
  std::puts("map_samples=150 legacy_profiles=4 custom_preserved=1 glyphs_follow=1");
}
