// SPDX-License-Identifier: MIT
// Native Orbis signal ABI. The bundled SDK has BSD sigaction/siginfo layouts
// but Linux signal numbers/flags and an incompatible stack_t/ucontext_t.
// ABI reference: FreeBSD releng/9.0 sys/sys/signal.h. Device acceptance remains
// explicit: tests/signal_probe.cpp checks registration and actual delivery.
#ifndef __ORBIS__
#error This backend is only for native Orbis
#endif
#include "port_crash.h"
#include "port_build_info.h"
#include "native_signal_context.hpp"
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>
extern "C" unsigned char __text_start[];

namespace {
constexpr int stack_signal=31; // BSD SIGUSR2; SDK's Linux value 12 is SIGSYS.
constexpr int native_siginfo=0x40,native_restart=0x2,native_onstack=0x1;
constexpr int faults[]={4,5,6,8,10,11,12}; // ILL, TRAP, ABRT, FPE, BUS, SEGV, native SYS
constexpr unsigned faultCount=sizeof(faults)/sizeof(faults[0]);
struct NativeAltStack { void *address;size_t size;int flags; };
static_assert(sizeof(NativeAltStack)==24 && offsetof(NativeAltStack,size)==8);
static_assert(sizeof(sigset_t)==16 && sizeof(struct sigaction)==32);
static_assert(offsetof(struct sigaction,sa_flags)==8 && offsetof(struct sigaction,sa_mask)==12);
static_assert(sizeof(siginfo_t)==80 && offsetof(siginfo_t,si_addr)==24);
static_assert(std::atomic<unsigned>::is_always_lock_free);
extern "C" int native_sigaltstack(const NativeAltStack *,NativeAltStack *) __asm__("sigaltstack");
struct sigaction previous[faultCount];
bool installed[faultCount]{};
std::atomic<unsigned> crashing{},stack_count{},ready{};
FILE *crash_file;
int crash_fd=-1;
alignas(16) unsigned char alternate[64*1024];

struct Line {
    char data[768];size_t used=0;
    Line &text(const char *s) { while (s && *s && used+1<sizeof(data)) data[used++]=*s++;return *this; }
    Line &number(uintptr_t v,unsigned base=10) {
        char reversed[32];unsigned n=0;
        do { reversed[n++]="0123456789abcdef"[v%base];v/=base; } while(v && n<sizeof(reversed));
        if (base==16) text("0x");
        while(n && used+1<sizeof(data)) data[used++]=reversed[--n];return *this;
    }
    void emit() {
        data[used++]='\n';
        for (int fd:{crash_fd,STDERR_FILENO}) {
            if (fd<0) continue;
            size_t done=0;
            while(done<used) {
                const auto n=write(fd,data+done,used-done);
                if (n>0) done+=size_t(n);
                else if(n<0 && errno==EINTR) continue;
                else break;
            }
        }
    }
};

_Unwind_Reason_Code frame(_Unwind_Context *context,void *user) {
    auto &count=*static_cast<unsigned *>(user);
    const uintptr_t pc=_Unwind_GetIP(context);
    if (!pc || count>=64) return _URC_END_OF_STACK;
    Line line;line.text("orbis: frame ").number(count++).text(" pc=").number(pc,16)
        .text(" app_base_delta=").number(pc>=uintptr_t(__text_start)?pc-uintptr_t(__text_start):pc,16).emit();
    return _URC_NO_REASON;
}
void trace() { unsigned count=0;_Unwind_Backtrace(frame,&count); }
void signal_context(siginfo_t* info,void* context) {
    prime::NativeSignalRegisters registers;
    const bool synchronous=info && uint32_t(info->si_code)<0x10000;
    if(context && uintptr_t(context)%alignof(uint64_t)==0 &&
       prime::native_signal_registers(static_cast<const uint64_t*>(context),64,
                                      synchronous,info?uintptr_t(info->si_addr):0,registers)) {
        Line line;line.text("orbis: native context rip=").number(registers.rip,16)
            .text(" rsp=").number(registers.rsp,16).text(" rbp=").number(registers.rbp,16)
            .text(" address=").number(registers.address,16).text(" flags=").number(registers.flags,16)
            .text(" trap=").number(registers.trap).text(" error=").number(registers.error)
            .text(" mcontext_word=").number(registers.contextWord).emit();
    } else {
        Line line;line.text("orbis: native context unavailable: BSD layout anchors not validated").emit();
    }
}
void stack_handler(int,siginfo_t *info,void *context) {
    const int saved=errno;
    Line line;line.text("orbis: watchdog main thread stack").emit();signal_context(info,context);trace();
    stack_count.fetch_add(1,std::memory_order_release);errno=saved;
}
void fault_handler(int signal,siginfo_t *info,void *context) {
    if (crashing.exchange(1)) std::_Exit(128+signal);
    Line line;line.text("orbis: fatal signal=").number(signal);
    if(info) line.text(" code=").number(uint32_t(info->si_code)).text(" address=").number(uintptr_t(info->si_addr),16);
    line.emit();
    Line build;build.text("orbis: build ").text(MP_BUILD_REVISION).emit();signal_context(info,context);trace();
    if (crash_fd>=0) fsync(crash_fd);
    for (unsigned i=0;i<faultCount;++i) if(installed[i]) sigaction(faults[i],&previous[i],nullptr);
    // Native BSD SI_USER/SI_LWP are positive 0x1000x values. Never apply the
    // Linux si_code<=0 rule, or an explicit abort would be swallowed.
    if (!info || info->si_code>=0x10000 || signal==6 || signal==5 || signal==12)
        pthread_kill(pthread_self(),signal);
    // A synchronous fault repeats on return, now under the previous handler.
}
}

extern "C" unsigned prime_ps4_stack_count() { return stack_count.load(std::memory_order_acquire); }
extern "C" unsigned prime_ps4_signal_ready() { return ready.load(std::memory_order_acquire); }

namespace PortCrash {
void Install() {
    static std::atomic<unsigned> once{};
    if (once.exchange(1)) return;
    const char* root=std::getenv("MP_PS4_LOG_ROOT");
    const std::string folder=root?root:"/data/project-prime/logs";
    std::rename((folder+"/crash.log").c_str(),(folder+"/crash.previous.log").c_str());
    crash_file=std::fopen((folder+"/crash.log").c_str(),"w");
    if(crash_file) crash_fd=fileno(crash_file);
    const NativeAltStack stack{alternate,sizeof(alternate),0};
    const bool alt=native_sigaltstack(&stack,nullptr)==0;
    Line abi;abi.text("orbis: native BSD signal ABI; context decoding requires validated anchors; altstack=")
        .number(alt).text(" app_image_base=").number(uintptr_t(__text_start),16).emit();
    // Warm up the unwinder outside a signal. No Linux ucontext fields are read.
    trace();
    struct sigaction action{};
    action.sa_sigaction=stack_handler;action.sa_flags=native_siginfo|native_restart|(alt?native_onstack:0);
    struct sigaction current{};
    if (sigaction(stack_signal,nullptr,&current)==0 &&
        (current.sa_handler==SIG_DFL || current.sa_handler==SIG_IGN) &&
        sigaction(stack_signal,&action,nullptr)==0) ready.store(1,std::memory_order_release);
    action.sa_sigaction=fault_handler;
    unsigned count=0;
    for(unsigned i=0;i<faultCount;++i) {
        installed[i]=sigaction(faults[i],&action,&previous[i])==0;
        count+=installed[i];
    }
    Line status;status.text("orbis: installed fault handlers=").number(count).text(" watchdog=").number(ready.load()).emit();
    if (const char *test=std::getenv("MP_CRASH_TEST")) {
        if (!std::strcmp(test,"abort")) std::abort();
        else if(!std::strcmp(test,"segv")) { volatile int *volatile bad=nullptr;*bad=1; }
    }
}
bool RequestStack(long thread) {
    if (!thread || !ready.load(std::memory_order_acquire)) return false;
    struct sigaction action{};
    if (sigaction(stack_signal,nullptr,&action) || action.sa_sigaction!=stack_handler) return false;
    return pthread_kill(static_cast<pthread_t>(thread),stack_signal)==0;
}
[[noreturn]] void AbortOnException(const char *where) {
    try { throw; }
    catch(const std::exception &e) { Line line;line.text("orbis: exception in ").text(where).text(": ").text(e.what()).emit(); }
    catch(...) { Line line;line.text("orbis: unknown exception in ").text(where).emit(); }
    std::abort();
}
}
