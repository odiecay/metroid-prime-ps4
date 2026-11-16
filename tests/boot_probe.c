// SPDX-License-Identifier: MIT
// Native SDK startup control: no game, SDL, Dawn, Vulkan, audio or C++ runtime.
#include <stddef.h>
extern void prime_ps4_loader_entry(void *) __attribute__((noreturn));
int main(void) { prime_ps4_loader_entry(NULL); }
