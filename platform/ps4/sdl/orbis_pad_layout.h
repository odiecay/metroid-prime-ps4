// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
enum { PRIME_PAD_GUIDE_INDEX=5,PRIME_PAD_LOGICAL_BUTTONS=15,PRIME_PAD_VIRTUAL_BUTTONS=14 };
static inline int prime_pad_virtual_button_index(int logical) {
    // SDL packs a virtual controller's present buttons contiguously. The PS
    // button belongs to the shell and is absent from this descriptor's mask.
    return logical==PRIME_PAD_GUIDE_INDEX ? -1 : logical<PRIME_PAD_GUIDE_INDEX ? logical : logical-1;
}
static inline int16_t prime_pad_stick_axis(uint8_t raw) {
    const int value=(int)raw-128;
    return (int16_t)(value<0 ? value*256 : value*32767/127);
}
