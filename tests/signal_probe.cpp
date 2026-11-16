// SPDX-License-Identifier: MIT
// Bounded, nonfatal native signal-delivery test; no fault is intentionally raised.
#include "port_crash.h"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <pthread.h>
#include <thread>
#include <orbis/libkernel.h>
extern const char kMpBuildRevision[]="project-prime-signal-probe";
extern const char kMpBuildVersion[]="00.01";
extern "C" unsigned prime_ps4_stack_count();
extern "C" unsigned prime_ps4_signal_ready();
int main() {
    std::error_code ec;std::filesystem::create_directories("/data/project-prime/logs",ec);
    FILE *f=std::fopen("/data/project-prime/signal.jsonl","w");if(!f) return 1;
    std::fprintf(f,"{\"schema\":1,\"target\":\"orbis\",\"probe\":\"native_signals\"}\n");std::fflush(f);
    PortCrash::Install();
    const long main_thread=static_cast<long>(pthread_self());
    std::atomic<bool> queued{};
    std::jthread sender([&] { queued.store(PortCrash::RequestStack(main_thread)); });
    const auto deadline=sceKernelGetProcessTime()+2000000;
    while(!prime_ps4_stack_count() && sceKernelGetProcessTime()<deadline) sceKernelUsleep(1000);
    sender.join();
    const bool passed=prime_ps4_signal_ready() && queued.load() && prime_ps4_stack_count()==1;
    std::fprintf(f,"{\"watchdog_handler_registered\":%s,\"signal_queued\":%s,\"stack_handlers_completed\":%u,\"passed\":%s}\n",
        prime_ps4_signal_ready()?"true":"false",queued.load()?"true":"false",prime_ps4_stack_count(),passed?"true":"false");
    std::fclose(f);return passed?0:1;
}
