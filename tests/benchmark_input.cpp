// SPDX-License-Identifier: MIT
// Compile the actual PAD implementation in this TU so the fixture can seed
// mappings without reading or writing a user's controller configuration.
#include PRIME_ACTUAL_PAD_CPP
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>

static unsigned leds=0,rumbles=0;
static bool keys[SDL_SCANCODE_COUNT]{};
// Only the host keyboard state is supplied; PADRead, mapping, merging and
// clamping execute the complete production implementation below/above.
extern "C" const bool* __wrap_SDL_GetKeyboardState(int* count){if(count)*count=SDL_SCANCODE_COUNT;return keys;}
static bool SDLCALL led(void*,Uint8,Uint8,Uint8){++leds;return true;}
static bool SDLCALL rumble(void*,Uint16,Uint16){++rumbles;return true;}
static PADStatus read_pad(){PADStatus pads[PAD_CHANMAX]{};PADRead(pads);return pads[0];}
static bool neutral_status(const PADStatus& p){return !p.button&&!p.extButton&&!p.stickX&&!p.stickY&&
    !p.substickX&&!p.substickY&&!p.triggerLeft&&!p.triggerRight&&!p.analogA&&!p.analogB;}
int main(){
    assert(SDL_Init(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.name="Prime input ownership regression";
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.SetLED=led;desc.Rumble=rumble;
    const auto id=SDL_AttachVirtualJoystick(&desc);assert(id);
    auto* joystick=SDL_OpenJoystick(id);auto* gamepad=SDL_OpenGamepad(id);assert(joystick&&gamepad);
    assert(SDL_SetGamepadPlayerIndex(gamepad,0));
    aurora::input::GameController controller;controller.m_controller=gamepad;controller.m_index=0;
    controller.m_mappingLoaded=true;controller.m_buttonMapping=g_defaultButtonsStandard;
    controller.m_axisMapping=g_defaultAxes;controller.m_hasRumble=true;controller.m_hasRgbLed=true;
    aurora::input::g_GameControllers.emplace(id,controller);
    g_initialized=true;g_keyboardBindingsLoaded=true;
    for(auto& bindings:g_keyboardBindings)bindings.m_slots.fill(PADKeyboardSlot{});
    g_keyboardBindings[0].m_mappingsSet=true;
    g_keyboardBindings[0].m_slots[0].m_buttonMapping[0]={SDL_SCANCODE_K,PAD_BUTTON_Y};
    g_keyboardBindings[0].m_slots[0].m_axisMapping[0]={SDL_SCANCODE_W,PAD_AXIS_LEFT_Y_POS,100};
    keys[SDL_SCANCODE_K]=keys[SDL_SCANCODE_W]=true;
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,32767));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_START,true));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    auto physical=read_pad();assert(physical.button&PAD_BUTTON_A);assert(physical.stickX>0);
    assert((physical.button&PAD_BUTTON_Y)&&physical.stickY>0);
    assert(physical.extButton&PAD_BUTTON_TOUCHPAD);
    PADStatus script{};script.err=PAD_ERR_NONE;
    PADSetVirtualStatus(0,&script);
    assert(read_pad().stickX==physical.stickX); // Old additive neutral reproduces the hole.
    PADSetVirtualStatusExclusive(0,&script);
    for(unsigned i=0;i<8;++i){const auto p=read_pad();assert(p.err==PAD_ERR_NONE&&neutral_status(p));}
    const auto oldLed=leds;aurora::input::g_GameControllers.at(id).m_isColorDirty=true;
    aurora::input::g_GameControllers.at(id).m_ledRed=37; // SDL coalesces identical LED values.
    read_pad();assert(leds>oldLed);
    PADControlMotor(0,PAD_MOTOR_RUMBLE);assert(rumbles>0);
    script.button=PAD_BUTTON_B;script.stickX=-127;script.substickY=100;script.triggerRight=200;
    PADSetVirtualStatusExclusive(0,&script);
    auto p=read_pad();assert(p.button==script.button&&p.stickX==-127&&p.extButton==0);
    PADStatus clamped[PAD_CHANMAX]{};clamped[0]=p;PADClamp(clamped);
    assert(clamped[0].stickX==-72&&clamped[0].substickY==44);
    PADBlockInput(true);auto blocked=read_pad();assert(blocked.extButton==physical.extButton);
    blocked.extButton=0;assert(neutral_status(blocked)); // Existing UI escape buttons remain available.
    PADBlockInput(false);p=read_pad();assert(p.button==script.button&&p.stickX==-127);
    PADSetVirtualStatusExclusive(PAD_CHANMAX,&script);PADSetVirtualStatusExclusive(0,nullptr);
    assert(read_pad().stickX==-127);
    PADClearVirtualStatus(0);assert(!g_virtualPadExclusive[0]);
    // Restore ordinary physical behaviour after the existing unblock suppression.
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_START,false));
    SDL_UpdateJoysticks();read_pad();
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));SDL_UpdateJoysticks();
    assert(read_pad().button&PAD_BUTTON_A);
    PADSetVirtualStatusExclusive(0,&script);PADSetVirtualStatus(0,&script);
    p=read_pad();assert((p.button&PAD_BUTTON_A)&&(p.button&PAD_BUTTON_B));
    PADClearAllVirtualStatus();for(bool owned:g_virtualPadExclusive)assert(!owned);
    PADStatus overrideStatus{};overrideStatus.err=PAD_ERR_NONE;overrideStatus.button=PAD_BUTTON_UP;
    PADSetPortOverride(0,&overrideStatus);assert(read_pad().button==PAD_BUTTON_UP);
    assert(read_pad().button&PAD_BUTTON_A); // Existing one-read override remains one-read.
    aurora::input::g_GameControllers.clear();SDL_CloseGamepad(gamepad);SDL_CloseJoystick(joystick);
    g_keyboardBindings[0].m_mappingsSet=false;
    assert(SDL_DetachVirtualJoystick(id));
    script={};script.err=PAD_ERR_NONE;PADSetVirtualStatusExclusive(0,&script);
    assert(read_pad().err==PAD_ERR_NONE);PADClearVirtualStatus(0);assert(read_pad().err==PAD_ERR_NO_CONTROLLER);
    SDL_Quit();std::puts("actual PADRead: additive/exclusive,8reads,physical/extended buttons,block,clamp,LED/rumble,release,disconnect passed");
}
