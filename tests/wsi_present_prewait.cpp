#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <sys/types.h>

#define WSI_ORBIS_MAX_BUFFERS 8
#define WSI_ORBIS_THROTTLE_NS (2ull * 1000 * 1000 * 1000)
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define mesa_loge(...) ((void)0)
#define mesa_logw(...) ((void)0)
#define ORBIS_BOOK_RETURN(id,start,value) do { (void)(start); return (value); } while (0)
struct OrbisVideoOutFlipStatus { int32_t numFlipPending; };
static std::atomic<uint64_t> clockNs;
static std::atomic<int> pending, errorCode, sleeps;
static std::atomic<bool> parked, allowReturn, blockSleep, destroyed;
static std::mutex gate;
static std::condition_variable changed;
static uint64_t os_time_get_nano() { return clockNs.load(); }
static int sceVideoOutGetFlipStatus(int video, OrbisVideoOutFlipStatus* status) {
    assert(video == 7); assert(!destroyed.load());
    status->numFlipPending=pending.load(); return errorCode.load();
}
static int sceKernelUsleep(uint32_t us) {
    ++sleeps;
    if (blockSleep.load()) {
        std::unique_lock lock(gate); parked=true; changed.notify_all();
        assert(changed.wait_for(lock,std::chrono::seconds(2),[]{return allowReturn.load();}));
    }
    clockNs.fetch_add(uint64_t(us)*1000);
    if (!blockSleep.load() && errorCode.load()==0 && pending.load()==3)
        pending=2;
    return 0;
}
#include "actual.inc"

static void reset() {
    clockNs=0; pending=0; errorCode=0; sleeps=0;
    parked=false;allowReturn=false;blockSleep=false;destroyed=false;
}
int main() {
    uint32_t before=99,capacity=99,after=99,polls=99;
    assert(wsi_orbis_prepare_present(&before,&capacity,&after,&polls)==0);
    assert(!before && !capacity && !after && !polls);
    wsi_orbis_scanout so{};so.video=7;so.count=4;so.owns_buffers=true;so.flips=99;
    prime_scanout_register(&so);
    reset();pending=3;
    assert(wsi_orbis_prepare_present(&before,&capacity,&after,&polls)==1);
    assert(before==3 && capacity==3 && after==2 && polls==2 && sleeps==1);
    assert(so.flips==99); // Advice never submits or retires a display buffer.
    // Readiness is no reservation: the authoritative path must query again.
    pending=3;sleeps=0;
    assert(wsi_orbis_wait_for_flip_slot(&so));assert(sleeps==1);
    reset();errorCode=-42;
    assert(wsi_orbis_prepare_present(nullptr,nullptr,nullptr,nullptr)==-1);
    assert(sleeps==0);
    reset();pending=8;
    assert(wsi_orbis_prepare_present(&before,&capacity,&after,&polls)==-2);
    assert(clockNs==20ull*1000*1000 && sleeps==40 && polls==41);
    wsi_orbis_scanout other=so;prime_scanout_register(&other);
    reset();pending=8;
    assert(wsi_orbis_prepare_present(nullptr,nullptr,nullptr,nullptr)==0 && sleeps==0);
    prime_scanout_unregister(&other);
    reset();so.owns_buffers=false;so.count=5;
    assert(wsi_orbis_scanout_reserve(&so,2)==4);
    pending=1;
    assert(wsi_orbis_prepare_present(&before,&capacity,&after,&polls)==1 && capacity==2);
    prime_scanout_unregister(&so);

    // Destroy must wait until the real advisory reader releases its native pin.
    auto* heap=new wsi_orbis_scanout{};
    heap->video=7;heap->count=4;heap->owns_buffers=true;
    prime_scanout_register(heap);reset();pending=3;blockSleep=true;
    std::thread reader([&]{assert(wsi_orbis_prepare_present(nullptr,nullptr,nullptr,nullptr)==1);});
    { std::unique_lock lock(gate);assert(changed.wait_for(lock,std::chrono::seconds(2),[]{return parked.load();})); }
    std::atomic<bool> attempted=false;
    std::thread closer([&]{attempted=true;prime_scanout_unregister(heap);destroyed=true;delete heap;});
    while (!attempted.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(!destroyed.load());
    { std::lock_guard lock(gate);pending=0;allowReturn=true; } changed.notify_all();
    reader.join();closer.join();assert(destroyed.load());
    assert(wsi_orbis_prepare_present(nullptr,nullptr,nullptr,nullptr)==0);
    // An overfull registry fails closed to ordinary authoritative presentation.
    wsi_orbis_scanout extra[9]{};
    for (auto& item:extra) prime_scanout_register(&item);
    assert(wsi_orbis_prepare_present(nullptr,nullptr,nullptr,nullptr)==0);
    for (auto& item:extra) prime_scanout_unregister(&item);
    std::puts("native_prewait ready/error/timeout/ambiguity/overflow=1 authoritative_recheck=1 lifetime_pin=1");
}
