// SPDX-License-Identifier: MIT
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <orbis/libkernel.h>

static_assert(sizeof(wchar_t)==2, "Match Orbis C wchar_t ABI");
static_assert(sizeof(void*)==8 && char(-1)<0);
static_assert(std::endian::native==std::endian::little);
static_assert(std::integral<unsigned> && std::invocable<void(*)()>);
static_assert(std::bit_cast<unsigned>(1.0f)==0x3f800000);

namespace {
std::atomic<unsigned> tls_destructed{};
struct ThreadMarker { unsigned value=0;~ThreadMarker() { if (value==17) tls_destructed.fetch_add(1); } };
thread_local ThreadMarker marker;
std::atomic<unsigned> static_initializers{};
int shared_static() {
    static const int value=[] {
        static_initializers.fetch_add(1);
        sceKernelUsleep(20000);
        return 37;
    }();
    return value;
}
}

int main() {
    std::error_code ec;
    std::filesystem::create_directories("/data/project-prime",ec);
    FILE *f=std::fopen("/data/project-prime/runtime-modern.jsonl","w");
    if (!f) { sceKernelDebugOutText(0,"Prime modern runtime: cannot open log\n"); return 1; }
    std::fprintf(f,"{\"schema\":1,\"target\":\"orbis\",\"runtime\":\"libc++19\",\"wchar_bytes\":%zu}\n",sizeof(wchar_t));
    std::fflush(f);
    bool passed=false;
    try {
        std::vector<int> values{7,2,5,1};
        std::ranges::sort(values);
        const bool ranges=values==std::vector<int>({1,2,5,7});
        const bool wide=std::wcslen(L"Prime")==5;
        const auto utf8=std::u8string(u8"/data/project-prime/Tallon-\u00e9");
        const bool paths=std::filesystem::path(utf8).u8string()==utf8;
        std::atomic<bool> started{false};
        const auto start=std::chrono::steady_clock::now();
        std::jthread worker([&](std::stop_token token) {
            started.store(true,std::memory_order_release);
            while (!token.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        worker.request_stop();worker.join();
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        std::atomic<bool> wait_finished{false};
        std::jthread watchdog([&] {
            const uint64_t deadline=sceKernelGetProcessTime()+5000000;
            while (!wait_finished.load(std::memory_order_acquire) && sceKernelGetProcessTime()<deadline)
                sceKernelUsleep(1000);
            if (!wait_finished.load(std::memory_order_acquire)) {
                std::fprintf(f,"{\"passed\":false,\"reason\":\"atomic wait watchdog expired\"}\n");
                std::fflush(f);std::_Exit(3);
            }
        });
        std::atomic<unsigned> word32{},entered{};
        std::atomic<uint64_t> word64{};
        std::jthread waiter32([&] { entered.fetch_add(1);word32.wait(0); });
        std::jthread waiter64([&] { entered.fetch_add(1);word64.wait(0); });
        while (entered.load()!=2) sceKernelUsleep(100);
        sceKernelUsleep(10000);
        word32.store(1);word32.notify_one();
        word64.store(1);word64.notify_all();
        waiter32.join();waiter64.join();
        const bool atomic_wait=word32.load()==1 && word64.load()==1;
        std::jthread tls_worker([] { marker.value=17; });
        tls_worker.join();
        const bool tls_exit=tls_destructed.load()==1;
        std::atomic<unsigned> static_ready{},static_ok{};
        std::atomic<bool> static_go{};
        std::vector<std::jthread> contenders;
        for (unsigned n=0;n<8;++n) contenders.emplace_back([&] {
            static_ready.fetch_add(1);
            while (!static_go.load(std::memory_order_acquire)) sceKernelUsleep(100);
            if (shared_static()==37) static_ok.fetch_add(1);
        });
        while (static_ready.load()!=8) sceKernelUsleep(100);
        static_go.store(true,std::memory_order_release);
        for (auto& contender:contenders) contender.join();
        const bool static_guard=static_ok.load()==8 && static_initializers.load()==1;
        wait_finished.store(true,std::memory_order_release);watchdog.join();
        bool unwind=false;
        try { throw std::runtime_error("Orbis unwind probe"); }
        catch (const std::runtime_error &e) { unwind=std::string(e.what())=="Orbis unwind probe"; }
        std::random_device entropy;
        const unsigned random_sample=entropy();
        (void)random_sample;
        passed=ranges && wide && paths && started.load() && ns>=5000000 && unwind && atomic_wait && tls_exit && static_guard;
        std::fprintf(f,"{\"ranges\":%s,\"wide_characters\":%s,\"utf8_paths\":%s,\"jthread\":%s,\"unwind\":%s,\"atomic_wait_notify_32_64\":%s,\"tls_destructor\":%s,\"static_guard_contention\":%s,\"native_random_read\":true,\"elapsed_ns\":%lld,\"passed\":%s}\n",
            ranges?"true":"false",wide?"true":"false",paths?"true":"false",started.load()?"true":"false",unwind?"true":"false",atomic_wait?"true":"false",tls_exit?"true":"false",static_guard?"true":"false",(long long)ns,passed?"true":"false");
    } catch (const std::exception &e) {
        sceKernelDebugOutText(0,e.what());
        std::fprintf(f,"{\"passed\":false,\"reason\":\"runtime exception; see kernel log\"}\n");
    }
    std::fflush(f);std::fclose(f);
    return passed?0:2;
}
