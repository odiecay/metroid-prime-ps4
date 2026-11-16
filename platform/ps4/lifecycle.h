// SPDX-License-Identifier: MIT
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void prime_ps4_lifecycle_install(void);
int prime_ps4_quit_requested(void);
void prime_ps4_request_quit(void);
void prime_ps4_shutdown_begin(const char *stage);
void prime_ps4_shutdown_stage(const char *stage);
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void prime_ps4_exit_to_shell(int status);
#ifdef __cplusplus
}
#endif
