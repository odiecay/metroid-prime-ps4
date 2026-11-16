// SPDX-License-Identifier: MIT
#include "lifecycle.h"
#include "shutdown_policy.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <csignal>
#include <cerrno>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>

#ifndef PRIME_SHUTDOWN_LOG
#define PRIME_SHUTDOWN_LOG "/data/project-prime/logs/shutdown.log"
#endif
namespace {
std::atomic<unsigned> quitRequested{};
std::atomic<unsigned> quitSignal{};
static_assert(std::atomic<unsigned>::is_always_lock_free);
std::atomic<bool> closing{}, exiting{};
std::atomic<uint64_t> began{};
std::atomic<const char*> phase{"running"};
int logFd=-1;
void record(const char *text) {
    char line[384];
    const int length=std::snprintf(line,sizeof(line),"orbis shutdown: %s\n",text);
    if (length<=0) return;
    const size_t size=static_cast<size_t>(length)<sizeof(line)?length:sizeof(line)-1;
    if (logFd>=0) { (void)write(logFd,line,size); (void)fsync(logFd); }
    sceKernelDebugOutText(0,line);
}
void term(int signal,siginfo_t* info,void*) {
    const int saved=errno;
    quitSignal.store(static_cast<unsigned>(signal),std::memory_order_relaxed);
    quitRequested.store(1,std::memory_order_relaxed);
    // Keep evidence even if the shell stops the other threads before the
    // monitor runs. No allocation, stdio, mutex or sysmodule call here.
    char line[160];size_t size=0;
    const auto text=[&](const char* value) { while(*value && size+1<sizeof(line)) line[size++]=*value++; };
    const auto number=[&](uint32_t value) {
        char digits[16];unsigned count=0;
        do { digits[count++]=char('0'+value%10);value/=10; } while(value);
        while(count && size+1<sizeof(line)) line[size++]=digits[--count];
    };
    text("orbis shutdown: received close signal=");number(unsigned(signal));
    if(info) { text(" code=");number(uint32_t(info->si_code));text(" sender=");number(uint32_t(info->si_pid)); }
    line[size++]='\n';
    if(logFd>=0) (void)write(logFd,line,size);
    errno=saved;
}
struct CloseSignalState {
    uintptr_t handler{};
    unsigned flags{};
    int maskResult{},blocked{},disposition{};
    bool operator==(const CloseSignalState&) const = default;
};
CloseSignalState close_signal_state() {
    sigset_t blocked{};
    const int maskResult=pthread_sigmask(0,nullptr,&blocked);
    struct sigaction action{};
    const int disposition=sigaction(15,nullptr,&action);
    return {uintptr_t(action.sa_sigaction),unsigned(action.sa_flags),maskResult,
            maskResult?-1:sigismember(&blocked,15),disposition};
}
void record_close_state(const char* role,int unblocked,const CloseSignalState& state) {
    char text[256];
    std::snprintf(text,sizeof(text),"close readiness role=%s unblock=%d mask_query=%d term_blocked=%d disposition=%d handler=%s address=0x%llx flags=0x%x",
                  role,unblocked,state.maskResult,state.blocked,state.disposition,
                  !state.disposition && state.handler==uintptr_t(term)?"native-close":"other",
                  static_cast<unsigned long long>(state.handler),state.flags);
    record(text);
}
void prepare_close_thread(const char* role) {
    sigset_t signals{};
    for(const int signal:{1,2,3,15}) sigaddset(&signals,signal);
#ifdef __ORBIS__
    constexpr int unblock=2;
#else
    constexpr int unblock=SIG_UNBLOCK;
#endif
    const int unblocked=pthread_sigmask(unblock,&signals,nullptr);
    record_close_state(role,unblocked,close_signal_state());
}
void monitor() {
    // Verify this thread itself: inherited masks are not an assumption about
    // the SDK's native pthread bootstrap or future worker creation changes.
    prepare_close_thread("monitor");
    auto signalState=close_signal_state();
    uint64_t nextAudit=0;
    prime::ShutdownDeadline deadline;
    for (;;) {
        const uint64_t now=sceKernelGetProcessTime();
        if(now>=nextAudit) {
            nextAudit=now+1000000;
            const auto current=close_signal_state();
            if(!(current==signalState)) {
                record_close_state("monitor-state-changed",0,current);
                signalState=current;
            }
        }
        if (quitRequested.load(std::memory_order_relaxed) && !closing.load(std::memory_order_acquire)) {
            char text[96];std::snprintf(text,sizeof(text),"close request signal=%u",quitSignal.load());record(text);
            prime_ps4_shutdown_begin("system close requested");
        }
        if (closing.load(std::memory_order_acquire))
            deadline.begin(began.load(std::memory_order_acquire));
        if (deadline.expired(now)) {
            record("cleanup deadline reached; resources retained for system termination");
            record(phase.load(std::memory_order_acquire));
            prime_ps4_exit_to_shell(0);
        }
        sceKernelUsleep(50000);
    }
}
}
extern "C" int prime_ps4_quit_requested() { return quitRequested.load(std::memory_order_relaxed)!=0; }
extern "C" void prime_ps4_request_quit() {
    record("in-game Quit requested");
    quitRequested.store(1,std::memory_order_release);
}
extern "C" void prime_ps4_shutdown_stage(const char *stage) {
    phase.store(stage,std::memory_order_release);
    record(stage);
}
extern "C" void prime_ps4_shutdown_begin(const char *stage) {
    // The first timestamp remains fixed across stages. Progress cannot extend
    // a hung shutdown forever. Called between ticks or by the close monitor.
    uint64_t expected=0;
    began.compare_exchange_strong(expected,sceKernelGetProcessTime(),std::memory_order_acq_rel);
    closing.store(true,std::memory_order_release);
    prime_ps4_shutdown_stage(stage);
}
extern "C" void prime_ps4_lifecycle_install() {
    const char* root=std::getenv("MP_PS4_LOG_ROOT");
    const std::string path=root?std::string(root)+"/shutdown.log":PRIME_SHUTDOWN_LOG;
    (void)rename(path.c_str(),(path+".previous").c_str());
    logFd=open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);
    struct sigaction action{};
    action.sa_sigaction=term;
    sigemptyset(&action.sa_mask);
#ifdef __ORBIS__
    constexpr int restart=0x2,siginfo=0x40; // Native BSD, not the SDK's Linux constants.
#else
    constexpr int restart=SA_RESTART,siginfo=SA_SIGINFO;
#endif
    action.sa_flags=restart|siginfo;
    // Native BSD SIGTERM is 15, matching the declaration in this SDK. The
    // fault handler remains separate; a close request is not a crash.
    for (const int signal : {1,2,3,15}) {
        if (sigaction(signal,&action,nullptr)) record("close signal handler could not be installed");
    }
    // A disposition alone does not deliver an inherited blocked signal. Both
    // this thread and the monitor created below can now receive close requests.
    prepare_close_thread("main");
    record("close signals unblocked; restartable waits enabled");
    record("native shell exit armed");
#ifndef PRIME_LIFECYCLE_TEST
    std::thread(monitor).detach();
#endif
}
extern "C" [[noreturn]] void prime_ps4_exit_to_shell(int status) {
    if (!exiting.exchange(true,std::memory_order_acq_rel)) {
        char text[96];
        std::snprintf(text,sizeof(text),"requesting shell exit, engine status=%d",status);
        record(text);
        const int module=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
        std::snprintf(text,sizeof(text),"SystemService module result=0x%08x",unsigned(module));
        record(text);
        const int result=sceSystemServiceLoadExec("exit",nullptr);
        std::snprintf(text,sizeof(text),"shell exit request returned=0x%08x",unsigned(result));
        record(text);
    }
    // LoadExec normally ends this title. If it returns, wait for the shell;
    // never fall through the libc _exit path or run live-worker destructors.
    for (;;) sceKernelUsleep(100000);
}
