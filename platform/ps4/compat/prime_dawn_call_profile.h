// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include <type_traits>

// A completed SubmitImpl observation. These are sampled CPU-side wall
// intervals in the Vulkan driver, never GPU execution or complete CPU cost.
struct PrimeDawnCallStats {
    uint64_t calls;
    uint64_t samples;
    uint64_t sampledWallNs;
    uint64_t invalidClocks;
    uint64_t cappedSamples;
};
extern "C" void prime_ps4_dawn_call_stats(const PrimeDawnCallStats*, unsigned)
    __attribute__((weak));
extern "C" void prime_ps4_dawn_frontend_stats(const PrimeDawnCallStats*, unsigned)
    __attribute__((weak));

namespace prime::dawn_call_profile {
// IDs0..3 are retained for existing driver observations. Frontend categories
// remain a separate namespace/TLS domain below.
enum class Category : unsigned {
    Draw=0, Descriptor=1, Pipeline=2, Barrier=3, PushConstants=4, DynamicState=5, Count=6
};
constexpr unsigned CategoryCount = unsigned(Category::Count);
constexpr uint32_t SamplePeriod = 64;
constexpr uint32_t MaximumSamplesPerCategory = 64;
using Observer = void (*)(const PrimeDawnCallStats*, unsigned);
using Clock = uint64_t (*)() noexcept;

inline uint64_t wall_now() noexcept {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline bool configured() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("MP_DAWN_CALL_PROFILE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Shared bounded sampling core for driver and frontend API domains. The
// frontend wraps API expressions by reference, without copying WGPU objects.
// These wall samples include any API lock wait and descheduling; they are not
// thread CPU time. No clock is queried on an unselected invocation.
namespace detail {
template<class Function, class... Args>
[[gnu::always_inline]] inline auto measure_row(PrimeDawnCallStats& row, uint32_t phase, Clock clock,
                                 Function function, Args... args) noexcept {
    using Result = decltype(function(std::forward<Args>(args)...));
    static_assert(!std::is_reference_v<Result>, "profiled calls return values or void");
    const uint64_t ordinal = row.calls++;
    if ((ordinal & (SamplePeriod - 1u)) != phase)
        return function(std::forward<Args>(args)...);
    if (row.samples + row.invalidClocks >= MaximumSamplesPerCategory) {
        ++row.cappedSamples;
        return function(std::forward<Args>(args)...);
    }
    const auto start = clock();
    const auto finish = [&] {
        const auto end = clock();
        if (start != std::numeric_limits<uint64_t>::max() &&
            end != std::numeric_limits<uint64_t>::max() && end >= start &&
            end - start <= std::numeric_limits<uint64_t>::max() - row.sampledWallNs) {
            ++row.samples;
            row.sampledWallNs += end - start;
        } else {
            ++row.invalidClocks;
        }
    };
    if constexpr (std::is_void_v<Result>) {
        function(std::forward<Args>(args)...);
        finish();
    } else {
        Result result = function(std::forward<Args>(args)...);
        finish();
        return result;
    }
}
} // namespace detail

class Batch;
inline thread_local Batch* currentBatch = nullptr;
inline thread_local uint32_t nextBatch = 0;

class Batch {
    std::array<PrimeDawnCallStats, CategoryCount> stats_;
    Batch* previous_;
    Observer observer_;
    Clock clock_;
    uint32_t phase_ = 0;
public:
    explicit Batch(bool enabled = configured(), Observer observer = prime_ps4_dawn_call_stats,
                   Clock clock = wall_now) noexcept
        : previous_(currentBatch), observer_(enabled && observer && clock ? observer : nullptr),
          clock_(clock) {
        currentBatch = observer_ ? this : nullptr;
        if (observer_) {
            stats_.fill({});
            phase_ = (nextBatch++ * 37u) & (SamplePeriod - 1u);
        }
    }
    ~Batch() {
        // Restore before observation: observer code cannot be counted in the
        // batch being completed. Nested/early-return scopes restore ownership.
        currentBatch = nullptr;
        if (observer_) observer_(stats_.data(), CategoryCount);
        currentBatch = previous_;
    }
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    Batch(Batch&&) = delete;
    Batch& operator=(Batch&&) = delete;

    template<class Function, class... Args>
    [[gnu::noinline]] void measure(Category category, Function function, Args... args) noexcept {
        const uint32_t phase = (phase_ + unsigned(category) * 17u) & (SamplePeriod - 1u);
        detail::measure_row(stats_[unsigned(category)], phase, clock_, function,
                            std::forward<Args>(args)...);
    }
};

// Keep the disabled branch small: no clocks, observer calls, allocation or
// category bookkeeping. The sampling/argument spill path is out of line.
template<class Function, class... Args>
inline void call(Category category, Function function, Args... args) noexcept {
    if (Batch* batch = currentBatch) {
        batch->measure(category, function, std::forward<Args>(args)...);
    } else {
        function(std::forward<Args>(args)...);
    }
}
// Independent API-family domain: never reuse Vulkan category IDs or its TLS
// scope. A frontend scope ends before Submit, whose own Vulkan batch remains
// authoritative for translation/driver observations.
namespace frontend {
enum class BatchKind : unsigned { Encoding, Present, Finish, Count };
enum class Category : unsigned { Pipeline, BindGroup, Immediates, Draw, DrawIndexed, Finish, Count };
constexpr unsigned CategoryCount = unsigned(Category::Count);
class Batch;
inline thread_local Batch* currentBatch = nullptr;
inline thread_local std::array<uint32_t,unsigned(BatchKind::Count)> nextBatch{};
class Batch {
    std::array<PrimeDawnCallStats, CategoryCount> stats_;
    Batch* previous_;
    Observer observer_;
    Clock clock_;
    uint32_t phase_ = 0;
public:
    explicit Batch(bool enabled = configured(), Observer observer = prime_ps4_dawn_frontend_stats,
                   Clock clock = wall_now, BatchKind kind = BatchKind::Encoding) noexcept
        : previous_(currentBatch), observer_(enabled && observer && clock ? observer : nullptr), clock_(clock) {
        currentBatch = observer_ ? this : nullptr;
        if (observer_) {
            stats_.fill({});
            phase_ = (nextBatch[unsigned(kind)]++ * 37u) & (SamplePeriod - 1u);
        }
    }
    ~Batch() {
        currentBatch = nullptr;
        if (observer_) observer_(stats_.data(), CategoryCount);
        currentBatch = previous_;
    }
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    Batch(Batch&&) = delete;
    Batch& operator=(Batch&&) = delete;
    template<class Function, class... Args>
    [[gnu::noinline]] auto measure(Category category, Function function, Args... args) noexcept {
        const uint32_t phase = (phase_ + unsigned(category) * 17u) & (SamplePeriod - 1u);
        return detail::measure_row(stats_[unsigned(category)], phase, clock_, function,
                                   std::forward<Args>(args)...);
    }
};
template<class Function, class... Args>
inline auto call(Category category, Function function, Args... args) noexcept {
    if (Batch* batch = currentBatch)
        return batch->measure(category, function, std::forward<Args>(args)...);
    return function(std::forward<Args>(args)...);
}
// Finish has a value result and occurs outside encode_op. End this batch and
// restore TLS before the returned command buffer reaches Queue::Submit.
template<class Function>
inline auto scoped(Category category, Function function) noexcept {
    Batch batch(configured(), prime_ps4_dawn_frontend_stats, wall_now, BatchKind::Finish);
    return call(category, function);
}
} // namespace frontend
} // namespace prime::dawn_call_profile

#define PRIME_DAWN_VK_CALL(category, function, ...) \
    ::prime::dawn_call_profile::call(::prime::dawn_call_profile::Category::category, function, __VA_ARGS__)
