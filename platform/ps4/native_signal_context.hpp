// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>

namespace prime {
struct NativeSignalRegisters {
    uint64_t rip{}, rsp{}, rbp{}, address{}, flags{}, error{};
    uint32_t trap{};
    size_t contextWord{};
};

// Orbis supplies a BSD context, not the SDK's Linux ucontext_t. The native
// orbis-compat reference measured CS=0x43 and SS=0x3b, three words apart,
// with mcontext beginning at context word 8 on hardware (2026-08-30).
// Find that layout rather than assuming a prefix; require independent flags,
// canonical user addresses and, for synchronous faults, the siginfo address.
inline bool native_signal_registers(const uint64_t* words,size_t count,
                                    bool checkAddress,uint64_t address,
                                    NativeSignalRegisters& output) noexcept {
    constexpr size_t Trap=16,Addr=17,Err=19,Rip=20,Cs=21,Flags=22,Rsp=23,Ss=24,Rbp=9;
    if(!words || count<Ss+1) return false;
    bool found=false;
    NativeSignalRegisters candidate;
    for(size_t cs=Cs;cs+Ss-Cs<count;++cs) {
        if(words[cs]!=0x43 || words[cs+Ss-Cs]!=0x3b) continue;
        const size_t base=cs-Cs;
        const auto rip=words[base+Rip],rsp=words[base+Rsp],flags=words[base+Flags];
        if(!rip || rip>=(uint64_t{1}<<47) || !rsp || rsp>=(uint64_t{1}<<47) || !(flags&2)) continue;
        if(checkAddress && words[base+Addr]!=address) continue;
        if(found) return false; // Ambiguous layouts must not become named PCs.
        candidate={rip,rsp,words[base+Rbp],words[base+Addr],flags,words[base+Err],
                   uint32_t(words[base+Trap]),base};
        found=true;
    }
    if(found) output=candidate;
    return found;
}
}
