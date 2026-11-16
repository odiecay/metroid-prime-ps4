#include "src/dawn/native/ImmediatesTracker.h"
#include "src/dawn/native/vulkan/ImmediatesLayoutVk.h"
#include "src/dawn/native/ImmediatesLayout.cpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace dawn;
using namespace dawn::native;
using namespace dawn::native::vulkan;

using VkPipelineLayout = uint64_t;
constexpr uint32_t kImmediateShaderStages = 0x31; // vertex|fragment|compute, as native layout.
struct Pipeline {
    ImmediateMask mask;
    VkPipelineLayout layout;
    const ImmediateMask& GetImmediateMask() const { return mask; }
    VkPipelineLayout GetVkLayout() const { return layout; }
};
Pipeline* ToBackend(Pipeline* pipeline) { return pipeline; }

struct Base : UserImmediatesTrackerBase<RenderImmediates, Pipeline> {
    void SetClampFragDepth(float minimum, float maximum) {
        UpdateImmediates(offsetof(RenderImmediates, clampFragDepth), ClampFragDepthArgs{minimum, maximum});
    }
};
struct Driver {
    std::array<uint32_t, 18> words{};
    std::array<uint32_t, 18> writtenRange{};
    std::array<uint32_t, 256> rangeSizes{};
    uint64_t calls = 0, bytes = 0;
};
using VkCommandBuffer = Driver*;
struct VulkanFunctions {
    void CmdPushConstants(VkCommandBuffer command, VkPipelineLayout layout, uint32_t stages,
                          uint32_t offset, uint32_t size, const void* values) const {
        assert(stages == kImmediateShaderStages && size > 0 && size % 4 == 0 && offset % 4 == 0);
        assert(layout < command->rangeSizes.size());
        const auto range = command->rangeSizes[layout];
        assert(offset + size <= range);
        std::memcpy(command->words.data() + offset / 4, values, size);
        for (unsigned word = offset / 4; word < (offset + size) / 4; ++word)
            command->writtenRange[word] = range;
        ++command->calls; command->bytes += size;
    }
};

#include "actual_immediate_tracker.inc"

template<typename T> struct LegacyTracker : ImmediateTracker<T> {
    template<typename P> void OnSetPipeline(P* pipeline) { T::OnSetPipeline(pipeline); }
};

template<typename T>
void verifyDraw(const T& tracker, const Driver& driver, const Pipeline& pipeline) {
    unsigned packed = 0;
    for (unsigned word = 0; word < sizeof(RenderImmediates) / 4; ++word) {
        if (!pipeline.mask[ImmediateIndex{word}]) continue;
        assert(driver.words[packed] == *tracker.GetContent().template Get<uint32_t>(word * 4));
        // Last writes must have a Vulkan-compatible complete push-constant range,
        // even when layout identities and descriptor layouts differ.
        assert(driver.writtenRange[packed] == pipeline.mask.count() * 4);
        ++packed;
    }
}

void correctness() {
    VulkanFunctions vk;
    Driver oldDriver, newDriver;
    VkCommandBuffer oldCommand = &oldDriver, newCommand = &newDriver;
    LegacyTracker<Base> before;
    ImmediateTracker<Base> after;
    std::vector<Pipeline> pipelines;
    const auto clamp = GetImmediateBlockBits(offsetof(RenderImmediates, clampFragDepth), sizeof(ClampFragDepthArgs));
    for (unsigned identity = 1; identity < 65; ++identity) {
        const auto mask = identity % 5 == 0 ? ImmediateMask{0} :
                          identity % 5 == 1 ? clamp :
                          identity % 5 == 2 ? ImmediateMask{3} :
                          identity % 5 == 3 ? ImmediateMask{15} :
                          ImmediateMask{15} | clamp;
        pipelines.push_back({mask, identity});
        oldDriver.rangeSizes[identity] = newDriver.rangeSizes[identity] = mask.count() * 4;
    }
    // Different pipeline objects/descriptor layouts with equal immediate ranges
    // inherit the current values. A sparse mask with the same population must
    // still cause an upload, because its compact word mapping is different.
    before.SetClampFragDepth(0.0f, 1.0f); after.SetClampFragDepth(0.0f, 1.0f);
    before.OnSetPipeline(&pipelines[0]); after.OnSetPipeline(&pipelines[0]);
    before.Apply(vk, oldCommand); after.Apply(vk, newCommand);
    const auto firstCalls = newDriver.calls;
    before.OnSetPipeline(&pipelines[5]); after.OnSetPipeline(&pipelines[5]);
    before.Apply(vk, oldCommand); after.Apply(vk, newCommand);
    assert(newDriver.calls == firstCalls && oldDriver.calls > newDriver.calls);
    verifyDraw(before, oldDriver, pipelines[5]); verifyDraw(after, newDriver, pipelines[5]);
    before.OnSetPipeline(&pipelines[1]); after.OnSetPipeline(&pipelines[1]);
    before.Apply(vk, oldCommand); after.Apply(vk, newCommand);
    assert(newDriver.calls > firstCalls); // same byte size, different sparse mapping
    verifyDraw(before, oldDriver, pipelines[1]); verifyDraw(after, newDriver, pipelines[1]);
    std::mt19937 random(0x108060);
    Pipeline* active = &pipelines[1];
    unsigned draws = 0;
    for (unsigned step = 0; step < 100000; ++step) {
        if (random() % 3 == 0) {
            const unsigned index = random() % 16;
            const uint32_t value = random();
            before.SetImmediates(index * 4, reinterpret_cast<const uint8_t*>(&value), 4);
            after.SetImmediates(index * 4, reinterpret_cast<const uint8_t*>(&value), 4);
        }
        if (random() % 11 == 0) {
            const float minimum = float(random() % 100) / 100;
            const float maximum = float(random() % 100) / 100;
            before.SetClampFragDepth(minimum, maximum); after.SetClampFragDepth(minimum, maximum);
        }
        if (random() % 17 == 0) { before.DirtyAll(); after.DirtyAll(); }
        // Include multiple pipeline changes before a draw, so unconsumed dirty
        // values and mask transitions must remain correct.
        for (unsigned change = random() % 4; change > 0; --change) {
            active = &pipelines[random() % pipelines.size()];
            before.OnSetPipeline(active); after.OnSetPipeline(active);
        }
        before.Apply(vk, oldCommand); after.Apply(vk, newCommand);
        verifyDraw(before, oldDriver, *active); verifyDraw(after, newDriver, *active);
        ++draws;
    }
    // Fresh pass/device state cannot inherit words from a previous tracker,
    // even when all pipeline addresses are unchanged.
    for (unsigned pass = 0; pass < 1000; ++pass) {
        ImmediateTracker<Base> fresh;
        Driver driver;
        driver.rangeSizes = newDriver.rangeSizes;
        VkCommandBuffer command = &driver;
        fresh.OnSetPipeline(&pipelines[0]); fresh.Apply(vk, command);
        assert(driver.calls == 1);
        verifyDraw(fresh, driver, pipelines[0]);
    }
    std::printf("immediate_draws=%u old_push_calls=%llu inherited_push_calls=%llu exact_values_and_ranges=1 fresh_passes=1000\n",
                draws, static_cast<unsigned long long>(oldDriver.calls),
                static_cast<unsigned long long>(newDriver.calls));
}

template<typename Tracker>
void benchmark(const char* label, bool changingUserData = false, bool empty = false) {
    std::array<Pipeline, 32> pipelines;
    Driver driver;
    VkCommandBuffer command = &driver;
    VulkanFunctions vk;
    auto mask = GetImmediateBlockBits(offsetof(RenderImmediates, clampFragDepth), sizeof(ClampFragDepthArgs));
    if (changingUserData) mask |= GetImmediateBlockBits(0, sizeof(UserImmediates));
    if (empty) mask = ImmediateMask{0};
    for (unsigned index = 0; index < pipelines.size(); ++index) {
        pipelines[index] = {mask, index + 1};
        driver.rangeSizes[index + 1] = mask.count() * 4;
    }
    std::vector<double> samples;
    for (unsigned frame = 0; frame < 300; ++frame) {
        Tracker tracker;
        tracker.SetClampFragDepth(0.0f, 1.0f);
        const auto start = std::chrono::steady_clock::now();
        for (unsigned draw = 0; draw < 1800; ++draw) {
            if (changingUserData)
                tracker.SetImmediates(0, reinterpret_cast<const uint8_t*>(&draw), sizeof(draw));
            tracker.OnSetPipeline(&pipelines[draw % pipelines.size()]);
            tracker.Apply(vk, command);
        }
        const auto end = std::chrono::steady_clock::now();
        if (frame >= 20) samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    std::printf("{\"benchmark\":\"%s\",\"median_us\":%.3f,\"p95_us\":%.3f,\"pushes_per_1800_draw_frame\":%llu}\n",
                label, samples[samples.size() / 2], samples[samples.size() * 95 / 100],
                static_cast<unsigned long long>(driver.calls / 300));
}
int main(int argc, char**) {
    if (argc > 1) {
        benchmark<LegacyTracker<Base>>("legacy");
        benchmark<ImmediateTracker<Base>>("inherited");
        benchmark<LegacyTracker<Base>>("legacy_changing", true);
        benchmark<ImmediateTracker<Base>>("inherited_changing", true);
        benchmark<LegacyTracker<Base>>("legacy_empty", false, true);
        benchmark<ImmediateTracker<Base>>("inherited_empty", false, true);
    } else correctness();
}
