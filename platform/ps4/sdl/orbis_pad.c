// SPDX-License-Identifier: MIT
// scePad input exposed through SDL's existing virtual gamepad API. All game
// remapping, deadzones, original/twin-stick presets and prompts stay upstream.
#include <SDL3/SDL.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "orbis_pad.h"
#include "orbis_pad_layout.h"
SDL_COMPILE_TIME_ASSERT(prime_guide_index,SDL_GAMEPAD_BUTTON_GUIDE==PRIME_PAD_GUIDE_INDEX);
SDL_COMPILE_TIME_ASSERT(prime_button_count,SDL_GAMEPAD_BUTTON_DPAD_RIGHT+1==PRIME_PAD_LOGICAL_BUTTONS);

static int handle = -1;
static bool initialized;
static Uint64 next_open;
static SDL_JoystickID instance;
static SDL_Joystick *joystick;
static SDL_Mutex *lock;

static bool SDLCALL rumble(void *user, Uint16 low, Uint16 high) {
    (void)user;
    OrbisPadVibeParam params = {.lgMotor = low >> 8, .smMotor = high >> 8};
    SDL_LockMutex(lock);
    int r = handle < 0 ? -1 : scePadSetVibration(handle, &params);
    SDL_UnlockMutex(lock);
    return r >= 0;
}

static void detach(void) {
    if (joystick) SDL_CloseJoystick(joystick);
    joystick = NULL;
    if (instance) SDL_DetachVirtualJoystick(instance);
    instance = 0;
}

void prime_ps4_pump_pad(void) {
    if (!lock) { lock = SDL_CreateMutex(); if (!lock) return; }
    SDL_LockMutex(lock);
    if (!initialized) {
        int params = 256;
        sceUserServiceInitialize(&params);
        int r = scePadInit();
        SDL_Log("Orbis scePadInit: 0x%08x", (unsigned)r);
        initialized = true;
    }
    if (handle < 0 && SDL_GetTicks() >= next_open) {
        next_open = SDL_GetTicks() + 1000;
        int user = -1;
        if (sceUserServiceGetInitialUser(&user) >= 0)
            handle = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    }
    OrbisPadData pad;
    SDL_zero(pad);
    int r = handle < 0 ? -1 : scePadReadState(handle, &pad);
    if (r < 0 && handle >= 0) { scePadClose(handle); handle = -1; }
    SDL_UnlockMutex(lock);
    if (r < 0 || !pad.connected) { detach(); return; }
    if (!instance) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = 0x054c; desc.product_id = 0x05c4;
        desc.name = "DualShock 4 (native Orbis)";
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = PRIME_PAD_VIRTUAL_BUTTONS;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        // Share/PS belong to the shell. Touchpad click supplies Back; the port
        // opens settings after holding it. Options remains the pause button.
        desc.button_mask = ((1u << PRIME_PAD_LOGICAL_BUTTONS) - 1) & ~(1u << SDL_GAMEPAD_BUTTON_GUIDE);
        desc.Rumble = rumble;
        instance = SDL_AttachVirtualJoystick(&desc);
        if (!instance) return;
        joystick = SDL_OpenJoystick(instance);
        if (!joystick) { detach(); return; }
        SDL_Log("Native DualShock 4 connected: %u", instance);
    }
    const Uint8 sticks[4] = {pad.leftStick.x, pad.leftStick.y, pad.rightStick.x, pad.rightStick.y};
    for (int i = 0; i < 4; ++i) {
        // Preserve exact neutral and both endpoints of the asymmetric range.
        SDL_SetJoystickVirtualAxis(joystick, i, prime_pad_stick_axis(sticks[i]));
    }
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, (Sint16)((int)pad.analogButtons.l2 * 257 - 32768));
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, (Sint16)((int)pad.analogButtons.r2 * 257 - 32768));
    const Uint32 buttons[15] = {
        ORBIS_PAD_BUTTON_CROSS, ORBIS_PAD_BUTTON_CIRCLE, ORBIS_PAD_BUTTON_SQUARE,
        ORBIS_PAD_BUTTON_TRIANGLE, ORBIS_PAD_BUTTON_TOUCH_PAD, 0,
        ORBIS_PAD_BUTTON_OPTIONS, ORBIS_PAD_BUTTON_L3, ORBIS_PAD_BUTTON_R3,
        ORBIS_PAD_BUTTON_L1, ORBIS_PAD_BUTTON_R1, ORBIS_PAD_BUTTON_UP,
        ORBIS_PAD_BUTTON_DOWN, ORBIS_PAD_BUTTON_LEFT, ORBIS_PAD_BUTTON_RIGHT};
    for (int i = 0; i < PRIME_PAD_LOGICAL_BUTTONS; ++i) {
        const int packed=prime_pad_virtual_button_index(i);
        if (packed>=0) SDL_SetJoystickVirtualButton(joystick, packed, (pad.buttons & buttons[i]) != 0);
    }
}

void prime_ps4_close_pad(void) {
    detach();
    if (!lock) return;
    SDL_LockMutex(lock);
    if (handle >= 0) {
        OrbisPadVibeParam off = {0};
        scePadSetVibration(handle, &off);
        scePadClose(handle);
        handle = -1;
    }
    SDL_UnlockMutex(lock);
    SDL_DestroyMutex(lock);
    lock = NULL;
    initialized = false;
}
