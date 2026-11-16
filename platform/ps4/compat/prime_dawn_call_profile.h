// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

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

namespace prime::dawn_call_profile {
enum class Category : unsigned { Draw, Descriptor, Pipeline, Barrier, Count };
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
        auto& row = stats_[unsigned(category)];
        const uint64_t ordinal = row.calls++;
        const uint32_t phase = (phase_ + unsigned(category) * 17u) & (SamplePeriod - 1u);
        if ((ordinal & (SamplePeriod - 1u)) != phase) {
            function(std::forward<Args>(args)...);
            return;
        }
        if (row.samples + row.invalidClocks >= MaximumSamplesPerCategory) {
            ++row.cappedSamples;
            function(std::forward<Args>(args)...);
            return;
        }
        const auto start = clock_();
        function(std::forward<Args>(args)...);
        const auto end = clock_();
        if (start != std::numeric_limits<uint64_t>::max() &&
            end != std::numeric_limits<uint64_t>::max() && end >= start &&
            end - start <= std::numeric_limits<uint64_t>::max() - row.sampledWallNs) {
            ++row.samples;
            row.sampledWallNs += end - start;
        } else {
            ++row.invalidClocks;
        }
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
} // namespace prime::dawn_call_profile

#define PRIME_DAWN_VK_CALL(category, function, ...) \
    ::prime::dawn_call_profile::call(::prime::dawn_call_profile::Category::category, function, __VA_ARGS__)
