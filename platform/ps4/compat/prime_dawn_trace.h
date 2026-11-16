// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include "prime_dawn_call_profile.h"
#include <stdint.h>
extern "C" void prime_ps4_dawn_duration(unsigned, uint64_t) __attribute__((weak));
class PrimeDawnScope {
    unsigned id_;
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
public:
    explicit PrimeDawnScope(unsigned id) : id_(id) {}
    ~PrimeDawnScope() {
        if(prime_ps4_dawn_duration)
            prime_ps4_dawn_duration(id_,std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start_).count());
    }
};
