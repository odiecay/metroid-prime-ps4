// SPDX-License-Identifier: MIT
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Call on the event thread before SDL_PollEvent, including while menus are up.
void prime_ps4_pump_pad(void);
void prime_ps4_close_pad(void);
#ifdef __cplusplus
}
#endif
