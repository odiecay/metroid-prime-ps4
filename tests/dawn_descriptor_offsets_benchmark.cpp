// Host microbenchmark of the extracted real descriptor emission blocks.
// It measures CPU bookkeeping with a consuming stub, not driver/GPU execution.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#define DAWN_ASSERT(x) assert(x)
constexpr uint32_t kMaxBindGroups = 4;
constexpr uint32_t kMaxDynamicUniformBuffersPerPipelineLayout = 16;
constexpr uint32_t kMaxDynamicStorageBuffersPerPipelineLayout = 16;
using BindGroupIndex = uint32_t;
using VkDescriptorSet = uint64_t;
const uint64_t* AsVkArray(const uint64_t* value) { return value; }
volatile uint64_t consumed = 0;
struct Driver {
    [[gnu::noinline]] void CmdBindDescriptorSets(unsigned, unsigned, unsigned, unsigned first,
                        unsigned count, const uint64_t* sets, unsigned offsetCount,
                        const uint32_t* offsets) {
        uint64_t value = first;
        for (unsigned i = 0; i < count; ++i) value += sets[i];
        for (unsigned i = 0; i < offsetCount; ++i) value += offsets[i];
        consumed = value;
    }
};
struct Fixture {
    Driver vk;
    unsigned commandBuffer = 1, bindPoint = 0, layout = 1, startOfBindGroups = 0;
    std::vector<BindGroupIndex> dirtyBindGroups;
    std::array<uint64_t, 4> groups{1, 2, 3, 4};
    std::array<std::array<uint32_t, 32>, 4> dynamicOffsets{};
    std::array<unsigned, 4> offsetCounts{};
    uint64_t calls = 0;
    void NoteDescriptorBind(uint32_t) { ++calls; }
    VkDescriptorSet GetDescriptorSet(unsigned index) { return groups[index]; }
    std::span<const uint32_t> GetDynamicOffsets(unsigned index) {
        return {dynamicOffsets[index].data(), offsetCounts[index]};
    }
    [[gnu::noinline]] void before() {
#include "descriptor-batch-v31.inc"
    }
    [[gnu::noinline]] void after() {
#include "descriptor-batch-fast.inc"
    }
};
int main() {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        Fixture fixture;
        fixture.dirtyBindGroups = scenario == 1 ? std::vector<unsigned>{1}
                                               : std::vector<unsigned>{1, 2};
        fixture.offsetCounts[1] = 1;
        if (scenario == 2) fixture.offsetCounts[2] = 2;
        std::array<std::vector<double>, 2> samples;
        constexpr unsigned iterations = 200000;
        for (unsigned repeat = 0; repeat < 14; ++repeat) {
            const bool fast = repeat % 2;
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                fixture.dynamicOffsets[1][0] = (i & 4095) * 256;
                if (fast) fixture.after(); else fixture.before();
            }
            const auto end = std::chrono::steady_clock::now();
            samples[fast].push_back(std::chrono::duration<double, std::nano>(end - start).count() /
                                    iterations);
        }
        for (auto& values : samples) std::sort(values.begin(), values.end());
        std::printf("{\"scenario\":\"%s\",\"v31_ns_per_draw\":%.3f,\"fast_ns_per_draw\":%.3f,\"iterations_per_sample\":%u,\"samples_per_mode\":7}\n",
                    scenario == 0 ? "uniform_plus_texture" : scenario == 1 ? "uniform_only" : "two_dynamic_groups",
                    samples[0][3], samples[1][3], iterations);
    }
}
