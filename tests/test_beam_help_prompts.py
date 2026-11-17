"""Exercise actual beam-chord generation, prompt selection and bounded glyph pixels."""
from pathlib import Path
import os
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

def fn(source, signature):
    begin=source.index(signature);brace=source.index('{',begin);depth=1;end=brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[begin:end]

class BeamHelpPromptTests(unittest.TestCase):
    def test_actual_beam_bindings_glyphs_and_borrowed_lifetimes(self):
        R=ROOT;O=ROOT/'build/beam-help-prompt-tests';O.mkdir(exist_ok=True)
        dev=Path(os.environ.get('BEAM_HELP_INPUT_SOURCE', R/'game/MetroidPrimePort/platform/input/port_input_devices.cpp')).read_text();pr=Path(os.environ.get('BEAM_HELP_PROMPT_SOURCE', R/'game/MetroidPrimePort/platform/port_prompts.cpp')).read_text();binding=(R/'game/MetroidPrimePort/platform/input/port_input_bindings.cpp').read_text()
        code=r'''
        using BOOL=int;
        #include "port_input_devices.h"
        #include <SDL3/SDL_gamepad.h>
        #include <SDL3/SDL_scancode.h>
        #include <algorithm>
        #include <atomic>
        #include <cassert>
        #include <cstdio>
        #include <cstring>
        #include <fstream>
        #include <map>
        #include <mutex>
        #include <set>
        #include <string>
        #include <thread>
        #include <unordered_map>
        #include <vector>
        using namespace PortInput;
        namespace PortInput {'''+fn(binding,'Family FamilyOf(')+'\n'+fn(binding,'Family BindingFamily(')+r'''
        }
        static bool twin=true,original=false,occupied=false;static int shift=-1;
        namespace PortDebug {bool TwinStick(){return twin;}bool OriginalExperience(){return original;}int ShiftBinding(int slot){return slot==PAD_KEY_SLOT_COUNT?shift:-1;}int MouseAction(int){return 0;}}
        namespace PortInputMap {constexpr int kMouseButtonCount=5,kMA_Shift=1;}
        namespace PortInputDevices {
        
        static uint16_t currentContexts=kCtxMenu;
        uint16_t GameContexts(){return currentContexts;}
        Input Make(Device device,uint16_t code){Input out;out.device=device;out.code=code;return out;}
        Input KeySlotInput(int,int){return {};}
        Input NativeInput(int native){return native<0?Input{}:Make(Device::PadButton,uint16_t(native));}
        bool PadButtonBoundBesidesL(int){return occupied;}
        void BindIn(Profile& p,Action action,Input in,uint16_t contexts){if(!in.Valid())return;Binding b;b.action=action;b.inputs[0]=in;b.count=1;b.contexts=contexts;p.bindings.push_back(b);}
        '''+fn(dev,'bool IsKeyboardFamily(')+'\n'+fn(dev,'bool IsPadFamily(')+'\n'+fn(dev,'void BindBeamChords(')+r'''
        static std::mutex sPromptMutex;static std::vector<Binding> sPromptBindings;
        '''+fn(dev,'static void UpdatePromptBindings(')+'\n'+fn(dev,'static bool PromptInputFor(')+'\n'+fn(dev,'bool PromptBeamBinding(')+'\n'+fn(dev,'static u32 PromptNative(')+'\n'+fn(dev,'u32 PromptNativeButton(')+r'''
        }
        '''
        a=pr.index('enum : uint32_t {');b=pr.index('\n};',a)+3;code+=pr[a:b]+'\n'+fn(pr,'const char* SuffixForSdlButton(')+r'''
        std::atomic<int> sBeamHelp{-1};
        '''+fn(pr,'std::string BeamInputStem(')+'\n'+fn(pr,'std::string BeamHelpStem(')+r'''
        static unsigned polls=0;
        namespace PortPrompts {void Poll(){++polls;}
        '''+fn(pr,'void SetBeamHelp(')+r'''
        }
        struct IconPixels {std::vector<uint8_t> rgba;uint32_t width=0,height=0;};
        static std::string glyphRoot;
        static bool ReadIconFile(const std::string& path,std::vector<uint8_t>& bytes){std::ifstream in(path,std::ios::binary);if(!in)return false;bytes={std::istreambuf_iterator<char>(in),{}};return true;}
        static const std::string* StablePath(const std::string& path){static std::set<std::string> paths;return &*paths.insert(path).first;}
        static std::string FindIconForSize(const std::string& part,uint32_t,uint32_t){auto path=glyphRoot+"/"+part+".dds";return std::ifstream(path).good()?path:std::string{};}
        '''+fn(pr,'const IconPixels* StablePixels(')+'\n'+fn(pr,'const IconPixels* StableChordPixels(')+r'''
        static Profile base(){Profile p;const Action acts[]={Action::PadUp,Action::PadDown,Action::PadLeft,Action::PadRight};const unsigned dirs[]={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};for(unsigned i=0;i<4;++i)PortInputDevices::BindIn(p,acts[i],PortInputDevices::Make(Device::PadButton,dirs[i]),kCtxAll);return p;}
        int main(int argc,char** argv){assert(argc==2);glyphRoot=argv[1];
         using namespace PortInputDevices;
         Profile p=base();BindBeamChords(p);const auto unchanged=p;UpdatePromptBindings(p);assert(p==unchanged);
         const char* dirs[]={"up","down","left","right"};const uint32_t prompts[]={PROMPT_CSTICK_UP,PROMPT_CSTICK_DOWN,PROMPT_CSTICK_LEFT,PROMPT_CSTICK_RIGHT};
         const Action visors[]={Action::PadUp,Action::PadDown,Action::PadLeft,Action::PadRight};
         for(unsigned i=0;i<4;++i){
          Binding b;assert(PromptBeamBinding(i,b)&&b.count==2&&b.inputs[0].code==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
          Input input;assert(PromptInputFor({visors[i]},Family::Pad,input));assert(input==b.inputs[1]);
          PortPrompts::SetBeamHelp(i);assert(BeamHelpStem(prompts[i],"playstation")==std::string("playstation_leftshoulder+playstation_dpad_")+dirs[i]);
          assert(BeamHelpStem(PROMPT_CSTICK,"playstation")=="playstation_leftshoulder+playstation_dpad");
          assert(BeamHelpStem(PROMPT_STICK,"playstation").empty());
          assert(BeamHelpStem(PROMPT_DPAD,"playstation")=="playstation_leftshoulder+playstation_dpad");
          assert(BeamHelpStem(prompts[i],"gamecube").empty()&&BeamHelpStem(prompts[i],"xbox").empty());
         }
         // A remapped modifier and direction follow the actual generated chord.
         shift=SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;p=base();p.bindings[0].inputs[0].code=SDL_GAMEPAD_BUTTON_DPAD_RIGHT;BindBeamChords(p);UpdatePromptBindings(p);
         PortPrompts::SetBeamHelp(0);assert(BeamHelpStem(PROMPT_CSTICK_UP,"playstation")=="playstation_rightshoulder+playstation_dpad_right");
         Binding owned;assert(PromptBeamBinding(0,owned));UpdatePromptBindings(Profile{});assert(owned.count==2&&owned.inputs[0].code==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);assert(!PromptBeamBinding(0,owned));
         // Classic controls and occupied modifier leave no invented default chord.
         shift=-1;twin=false;p=base();BindBeamChords(p);UpdatePromptBindings(p);assert(!PromptBeamBinding(0,owned));PortPrompts::SetBeamHelp(0);assert(sBeamHelp==-1);
         twin=true;occupied=true;p=base();BindBeamChords(p);UpdatePromptBindings(p);assert(!PromptBeamBinding(0,owned));occupied=false;
         p=base();BindBeamChords(p);UpdatePromptBindings(p);PortPrompts::SetBeamHelp(1);original=true;PortPrompts::SetBeamHelp(1);assert(sBeamHelp==-1);original=false;
         // User action rows override a chord; invalid/layer-only/blocked rows do not win.
         Binding direct;direct.action=Action::BeamPower;direct.inputs[0]=Make(Device::PadButton,SDL_GAMEPAD_BUTTON_NORTH);direct.count=1;direct.contexts=kCtxGameplay;
         Profile custom;custom.bindings.push_back(direct);UpdatePromptBindings(custom);PortPrompts::SetBeamHelp(0);assert(BeamHelpStem(PROMPT_CSTICK_UP,"playstation")=="playstation_north");
         direct.blocked=kCtxGameplay;custom.bindings[0]=direct;UpdatePromptBindings(custom);assert(!PromptBeamBinding(0,owned));
         direct.blocked=0;direct.contexts=kCtxLayer1;custom.bindings[0]=direct;UpdatePromptBindings(custom);assert(!PromptBeamBinding(0,owned));
         direct.count=5;custom.bindings[0]=direct;UpdatePromptBindings(custom);assert(!PromptBeamBinding(0,owned));
         // Repeated context toggles release help immediately; no stale map/camera icon.
         for(unsigned i=0;i<1000;++i){PortPrompts::SetBeamHelp(0);PortPrompts::SetBeamHelp(-1);assert(BeamHelpStem(PROMPT_CSTICK_UP,"playstation").empty());}
         const IconPixels* first=nullptr;std::vector<uint8_t> firstBytes;
         for(const auto width:{32u,64u})for(unsigned i=0;i<4;++i){
          const auto stem=std::string("playstation_leftshoulder+playstation_dpad_")+dirs[i];const auto* pixels=StableChordPixels(stem,width,32);assert(pixels&&pixels->width==width*2&&pixels->height==64);
          assert(StableChordPixels(stem,width,32)==pixels);
          if(!first){first=pixels;firstBytes=first->rgba;}
          for(uint32_t y=0;y<pixels->height;++y)for(uint32_t x=0;x<pixels->width;++x)if(pixels->rgba[(size_t(y)*pixels->width+x)*4+3]){assert(x>pixels->width/5&&x<pixels->width-pixels->width/5);assert(y>pixels->height/12&&y<pixels->height-pixels->height/12);}
         }
         // Fill the bounded cache with supported size variants; old borrowed texels
         // remain owned and immutable even after the table rehashes.
         unsigned made=0;
         for(uint32_t width=16;width<=128;++width)for(uint32_t height=16;height<=64;++height){if(StableChordPixels("playstation_leftshoulder+playstation_dpad",width,height))++made;}
         assert(made<=128);assert(first->rgba==firstBytes);assert(!StableChordPixels("bad+missing",64,32));assert(!StableChordPixels("a+b+c",64,32));assert(!StableChordPixels("a+b",UINT32_MAX,32));
         std::printf("beam_help directions=4 visor_singles=4 remapped_modifier_direction=1 snapshots_owned=1 classic_map_fallback=1 cache_entries_bounded=128 texels_immutable=1\n");
        }
        '''
        unit=O/'actual_beam_help.cpp';unit.write_text(code)
        header_root=Path(os.environ.get('BEAM_HELP_HEADERS',R/'game/MetroidPrimePort/platform/include'))
        for sanitized in (False,True):
            program=O/('sanitized' if sanitized else 'normal')
            args=['g++-14','-std=c++20','-O2','-D__ORBIS__','-DTARGET_PC','-I'+str(header_root),
                  '-I'+str(R/'game/MetroidPrimePort/platform/include'),
                  '-I'+str(R/'game/MetroidPrimePort/extern/aurora/include'),
                  '-I'+str(R/'external/SDL-release-3.4.10/include'),str(unit),'-o',str(program)]
            if sanitized:args+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
            subprocess.run(['taskset','-c','0-2,4-6']+args,check=True)
            result=subprocess.run([str(program),str(R/'game/MetroidPrimePort/textures/bindings')],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('directions=4 visor_singles=4 remapped_modifier_direction=1',result.stdout)
            self.assertIn('cache_entries_bounded=128 texels_immutable=1',result.stdout)
            print(result.stdout.strip())

    def test_help_scope_is_limited_to_visible_inventory_beam_body(self):
        source=Path(os.environ.get('BEAM_HELP_INVENTORY_SOURCE',ROOT/'game/MetroidPrimePort/src/MetroidPrime/CInventoryScreen.cpp')).read_text()
        update=fn(source,'void CInventoryScreen::Update(')
        for guard in ('x1a8_state == kS_Active','x10_mode == kM_TextScroll','x1ad_textViewing',
                      'x1a4_textBodyAlpha > 0.f','GetViewInterpolation()) == 0.f',
                      'GetUserSelection() == 0','x1c_rightSel >= 0','x1c_rightSel < 4'):
            self.assertIn(guard,update)
        self.assertIn('directions[] = {0, 1, 3, 2}',update)
        for signature in ('void CInventoryScreen::TransitioningAway(', 'CInventoryScreen::~CInventoryScreen('):
            self.assertIn('PortPrompts::SetBeamHelp(-1);',fn(source,signature))
        # Existing localized descriptions and their animated-image tags are
        # still read from the user's disc; no English prose is substituted.
        text=fn(source,'void CInventoryScreen::UpdateTextBody(')
        self.assertIn('xc_pauseStrg.GetString(item.entryStrIdx)',text)
        self.assertNotIn('PortPrompts',text)

if __name__=='__main__':unittest.main()
