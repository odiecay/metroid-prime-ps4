// SPDX-License-Identifier: MIT
#pragma once
#include "port_input_engine.h"

namespace prime::input {
inline PortInput::Binding port_menu_shortcut(PortInput::Input touchpad) {
    PortInput::Binding binding;
    binding.action=PortInput::Action::PortMenu;
    binding.inputs[0]=touchpad;
    binding.count=1;
    binding.trigger=PortInput::Trigger::Hold;
    binding.holdMs=800;
    return binding;
}
}
