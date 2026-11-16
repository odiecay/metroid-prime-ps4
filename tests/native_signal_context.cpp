#include "native_signal_context.hpp"
#include <array>
#include <cassert>
#include <cstdio>
using Words=std::array<uint64_t,64>;
static void Context(Words& words,size_t base) {
    words[base+9]=0x880123f00;
    words[base+16]=12;
    words[base+17]=0x12340000;
    words[base+19]=4;
    words[base+20]=0x9950a1;
    words[base+21]=0x43;
    words[base+22]=0x202;
    words[base+23]=0x880123ed0;
    words[base+24]=0x3b;
}
int main() {
    prime::NativeSignalRegisters output;
    for(size_t base:{size_t(0),size_t(2),size_t(8),size_t(17)}) {
        Words words{};Context(words,base);
        assert(prime::native_signal_registers(words.data(),words.size(),true,0x12340000,output));
        assert(output.rip==0x9950a1 && output.rsp==0x880123ed0 && output.rbp==0x880123f00);
        assert(output.trap==12 && output.error==4 && output.contextWord==base);
        assert(!prime::native_signal_registers(words.data(),words.size(),true,0x56780000,output));
        assert(prime::native_signal_registers(words.data(),words.size(),false,0,output));
        words[base+22]=0;
        assert(!prime::native_signal_registers(words.data(),words.size(),false,0,output));
    }
    Words words{};Context(words,8);words[8+21]=0x33;words[8+24]=0x2b;
    assert(!prime::native_signal_registers(words.data(),words.size(),true,0x12340000,output));
    words={};Context(words,8);words[8+20]=uint64_t{1}<<48;
    assert(!prime::native_signal_registers(words.data(),words.size(),false,0,output));
    words={};Context(words,2);Context(words,32);
    assert(!prime::native_signal_registers(words.data(),words.size(),false,0,output));
    assert(!prime::native_signal_registers(nullptr,64,false,0,output));
    assert(!prime::native_signal_registers(words.data(),24,false,0,output));
    std::puts("native BSD context: variable prefix, independent anchors, fault-address validation, ambiguity and Linux-layout rejection PASS");
}
