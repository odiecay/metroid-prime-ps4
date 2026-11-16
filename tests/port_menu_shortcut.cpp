#include "port_menu_shortcut.hpp"
#include <cassert>
#include <cstdio>
using namespace PortInput;

int main() {
    const Input touch{Device::PadButton,0,50,4};
    const Input options{Device::PadButton,0,50,6};
    Profile profile;
    profile.bindings.push_back(prime::input::port_menu_shortcut(touch));
    Binding pause;pause.action=Action::PadStart;pause.inputs[0]=options;pause.count=1;
    profile.bindings.push_back(pause);
    Runtime runtime;runtime.SetProfile(&profile);
    RawState raw;
    const auto poll=[&](double ms,uint16_t context=kCtxGameplay)->const Output& {
        return runtime.Poll(raw,context,ms,0);
    };
    poll(0);
    raw.padButtons=1u<<6;
    auto out=poll(10);assert(out.Pressed(Action::PadStart));assert(!out.Held(Action::PortMenu));
    assert(!poll(2010).Held(Action::PortMenu));
    raw.padButtons=0;poll(2020);
    raw.padButtons=1u<<4;assert(!poll(2100).Held(Action::PortMenu));
    raw.padButtons=0;assert(!poll(2500).Pressed(Action::PortMenu));
    raw.padButtons=1u<<4;assert(!poll(3000).Held(Action::PortMenu));
    assert(!poll(3799).Pressed(Action::PortMenu));
    assert(poll(3800).Pressed(Action::PortMenu));
    assert(!poll(3810).Pressed(Action::PortMenu));
    assert(!poll(5000,kCtxMenu).Pressed(Action::PortMenu));
    assert(!poll(7000,kCtxMenu).Pressed(Action::PortMenu));
    // Disconnect/release clears the previous hold; reconnect needs a new hold.
    raw.padButtons=0;assert(!poll(7010).Pressed(Action::PortMenu));
    raw.padButtons=1u<<4;assert(!poll(8000).Pressed(Action::PortMenu));
    assert(poll(8800).Pressed(Action::PortMenu));
    // The old combination is not the shortcut, and Options can still pause.
    raw.padButtons=0;poll(9000);
    raw.padButtons=(1u<<4)|(1u<<6);
    out=poll(9010);assert(out.Pressed(Action::PadStart));assert(!out.Pressed(Action::PortMenu));
    raw.padButtons=0;poll(10000);
    raw.padButtons=1u<<4;assert(!poll(10100).Pressed(Action::PortMenu));
    assert(!poll(10500,kCtxMenu).Pressed(Action::PortMenu));
    assert(poll(10900,kCtxMenu).Pressed(Action::PortMenu));
    std::puts("800ms touchpad hold: one toggle per hold, short taps/Options safe, context/release/reconnect passed");
}
