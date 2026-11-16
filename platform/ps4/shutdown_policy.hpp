// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace prime {
struct ShutdownDeadline {
    bool armed = false;
    uint64_t started = 0;
    void begin(uint64_t now) {
        if (!armed) { started=now; armed=true; }
    }
    bool expired(uint64_t now) const {
        return armed && now>=started && now-started>=8000000;
    }
};
}
