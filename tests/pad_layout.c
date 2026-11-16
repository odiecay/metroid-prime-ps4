// SPDX-License-Identifier: MIT
#include "../platform/ps4/sdl/orbis_pad_layout.h"
#include <assert.h>
int main(void) {
    assert(prime_pad_stick_axis(0)==-32768);
    assert(prime_pad_stick_axis(128)==0);
    assert(prime_pad_stick_axis(255)==32767);
    for (unsigned i=1;i<256;++i) assert(prime_pad_stick_axis(i)>prime_pad_stick_axis(i-1));
    unsigned seen=0;
    for (int i=0;i<PRIME_PAD_LOGICAL_BUTTONS;++i) {
        int packed=prime_pad_virtual_button_index(i);
        if (i==PRIME_PAD_GUIDE_INDEX) { assert(packed==-1);continue; }
        assert(packed>=0 && packed<PRIME_PAD_VIRTUAL_BUTTONS);
        assert(!(seen&(1u<<packed)));seen|=1u<<packed;
    }
    assert(seen==((1u<<PRIME_PAD_VIRTUAL_BUTTONS)-1));
    assert(prime_pad_virtual_button_index(6)==5); // Options/Start
    assert(prime_pad_virtual_button_index(14)==13); // D-pad right
}
