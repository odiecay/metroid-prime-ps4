#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

// The fixture supplies native display/device services. Both present call
// blocks are extracted verbatim from production; no GPU/display is emulated.
namespace wgpu {
enum class Status { Error, Success };
struct ConvertibleStatus {
    Status status;
    ConvertibleStatus(Status value) : status(value) {}
    explicit operator bool() const { return status == Status::Success; }
};
}
using MaybeError = bool;
#define DAWN_INVALID_IF(condition, ...) if (condition) return true
#define DAWN_VALIDATION_ERROR(...) true
#define DAWN_TRY(expression) if (expression) return true
#define ZoneScopedN(...)

static std::mutex deviceMutex, windowMutex, signalMutex;
static std::condition_variable signal;
static thread_local bool deviceOwned = false, windowOwned = false;
static bool ready = false;
static unsigned parked = 0;
static bool advisoryEnabled = true, presentable = true, surfaceExists = true, deviceExists = true;
static int advisoryResult = 1;
static unsigned advisoryCalls = 0, originalCalls = 0, gpuChecks = 0, flipChecks = 0;
static unsigned ownerToken = 7, queued = 0, afterPresent = 0, releases = 0;

struct DeviceGuard {
    std::unique_lock<std::mutex> lock{deviceMutex};
    DeviceGuard() { assert(!deviceOwned); deviceOwned = true; }
    ~DeviceGuard() { deviceOwned = false; }
};
struct Device {
    DeviceGuard GetGuard() { return {}; }
    bool ConsumedError(bool error) { return error; }
} device;
struct Instance { bool ConsumedError(bool error) { return error; } } instance;

static void wait_display(unsigned where) {
    std::unique_lock lock{signalMutex};
    if (!ready) {
        parked = where;
        signal.notify_one();
        signal.wait(lock, [] { return ready; });
    }
}
struct SwapChain {
    SwapChain* Get() { return this; }
    SwapChain* operator->() { return this; }
    bool Present() {
        assert(deviceOwned && windowOwned);
        ++originalCalls;
        ++gpuChecks;
        // The original driver remains the sole writer of image ownership.
        ++flipChecks;
        wait_display(2);
        ++ownerToken;
        return false;
    }
};
class Surface {
    SwapChain mSwapChain;
    Instance* mInstance = &instance;
    Device* GetCurrentDevice() { return deviceExists ? &device : nullptr; }
    bool IsError() const { return false; }
public:
    wgpu::Status APIPresent();
};
#include "actual_dawn_present.inc"
struct SurfaceHandle {
    Surface surface;
    explicit operator bool() const { return surfaceExists; }
    wgpu::Status Present() { return surface.APIPresent(); }
} g_surface;
namespace window {
struct SurfaceLock {
    std::unique_lock<std::mutex> lock{windowMutex};
    SurfaceLock() { assert(!windowOwned); windowOwned = true; }
    ~SurfaceLock() { windowOwned = false; }
};
bool is_presentable() { assert(windowOwned); return presentable; }
}
namespace phase { enum Lane { Render }; void set(Lane,const char*) {} }
namespace gfx { void after_present() { ++afterPresent; } }
namespace webgpu { void release_surface() { ++releases; } }
struct Logger { void warn(const char*) {} } Log;
namespace aurora {
void prime_orbis_prepare_present() {
    assert(windowOwned && !deviceOwned);
    const auto owned = ownerToken;
    ++advisoryCalls;
    if (advisoryEnabled && advisoryResult == 1) wait_display(1);
    assert(ownerToken == owned); // No acquire/retire/flip/ownership mutation.
}
void prime_orbis_present_queued() { assert(!deviceOwned); ++queued; }
}
static void render(bool canPresent) {
#include "actual_aurora_present.inc"
}

static void reset() {
    ready = false; parked = 0;
    advisoryCalls = originalCalls = gpuChecks = flipChecks = queued = afterPresent = releases = 0;
    ownerToken = 7; advisoryEnabled = presentable = surfaceExists = deviceExists = true;
    advisoryResult = 1;
}

static void concurrent_case(bool enabled, int result) {
    reset(); advisoryEnabled = enabled; advisoryResult = result;
    std::thread renderer([] { render(true); });
    {
        std::unique_lock lock{signalMutex};
        assert(signal.wait_for(lock, std::chrono::seconds{2}, [] { return parked != 0; }));
    }
#ifdef __ORBIS__
    const bool prewait = enabled && result == 1;
#else
    const bool prewait = false;
#endif
    assert(parked == (prewait ? 1u : 2u));
    // A producer requiring the real device lock can progress while the
    // advisory prewait is parked. The baseline blocks it inside Present.
    const bool producerCanProgress = deviceMutex.try_lock();
    assert(producerCanProgress == prewait);
    if (producerCanProgress) deviceMutex.unlock();
    assert(ownerToken == 7);
    {
        std::lock_guard lock{signalMutex};
        ready = true;
    }
    signal.notify_one(); renderer.join();
    assert(originalCalls == 1 && gpuChecks == 1 && flipChecks == 1 && ownerToken == 8);
    assert(afterPresent == 1 && releases == 0);
#ifdef __ORBIS__
    assert(advisoryCalls == 1 && queued == 1);
#else
    assert(advisoryCalls == 0 && queued == 0);
#endif
}

int main() {
    for (unsigned repeat = 0; repeat < 100; ++repeat) {
        concurrent_case(true, 1);
        concurrent_case(false, 1);
        for (int result : {0, -1, -2}) concurrent_case(true, result);
    }
    reset(); render(false); assert(originalCalls == 0 && advisoryCalls == 0);
    reset(); surfaceExists = false; render(true); assert(originalCalls == 0 && advisoryCalls == 0);
    reset(); presentable = false; render(true);
    assert(originalCalls == 0 && advisoryCalls == 0 && releases == 1);
    reset(); ready = true; deviceExists = false; render(true);
    assert(originalCalls == 0 && releases == 1 && ownerToken == 7);
    std::printf("present_cases=504 unchanged_authoritative_present=1 ownership_checks=1 native=%u\n",
#ifdef __ORBIS__
        1u
#else
        0u
#endif
    );
}
