#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

static uint64_t allocations = 0;
void* operator new(std::size_t size) {
    ++allocations;
    if (void* result = std::malloc(size)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

using VkPipelineStageFlags = uint32_t;
struct VkImageMemoryBarrier {
    std::array<uint64_t, 10> words;
    bool operator==(const VkImageMemoryBarrier&) const = default;
};
struct ImageBarriers {
    std::vector<VkImageMemoryBarrier> imageBarriers;
    VkPipelineStageFlags srcStages = 0;
    VkPipelineStageFlags dstStages = 0;
};
template<typename T, typename U> T checked_cast(U value) { return static_cast<T>(value); }
struct Captured {
    uint32_t src, dst, count;
    std::array<VkImageMemoryBarrier, 32> images;
    bool operator==(const Captured&) const = default;
};
struct Functions {
    unsigned count = 0;
    std::array<Captured, 2> calls{};
    void CmdPipelineBarrier(unsigned commandBuffer, uint32_t src, uint32_t dst,
                            uint32_t dependencies, uint32_t memoryCount, const void* memory,
                            uint32_t bufferCount, const void* buffers, uint32_t imageCount,
                            const VkImageMemoryBarrier* images) {
        assert(commandBuffer == 100 && dependencies == 0 && memoryCount == 0 && !memory);
        assert(bufferCount == 0 && !buffers && imageCount > 0 && imageCount <= 32);
        assert(count < calls.size());
        auto& call = calls[count++];
        call.src = src; call.dst = dst; call.count = imageCount;
        std::copy_n(images, imageCount, call.images.begin());
    }
};
struct Device { Functions fn; };
struct Context { unsigned commandBuffer = 100; };

void old(Device* device, Context* recordingContext,
         const ImageBarriers& vertexImageBarriers, const ImageBarriers& nonVertexImageBarriers) {
    for (const ImageBarriers& barriers : {vertexImageBarriers, nonVertexImageBarriers}) {
        if (!barriers.imageBarriers.empty()) {
            device->fn.CmdPipelineBarrier(recordingContext->commandBuffer, barriers.srcStages,
                                          barriers.dstStages, 0, 0, nullptr, 0, nullptr,
                                          checked_cast<uint32_t>(barriers.imageBarriers.size()),
                                          barriers.imageBarriers.data());
        }
    }
}
void current(Device* device, Context* recordingContext,
             const ImageBarriers& vertexImageBarriers, const ImageBarriers& nonVertexImageBarriers) {
#include "actual_barrier_batch.inc"
}
int main() {
    std::mt19937_64 random(0x108060);
    uint64_t oldAllocations = 0, currentAllocations = 0;
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        ImageBarriers vertexImageBarriers, nonVertexImageBarriers;
        for (ImageBarriers* batch : {&vertexImageBarriers, &nonVertexImageBarriers}) {
            batch->srcStages = uint32_t(random());
            batch->dstStages = uint32_t(random());
            batch->imageBarriers.resize(random() % 33);
            for (auto& barrier : batch->imageBarriers) {
                for (auto& word : barrier.words) word = random();
            }
        }
        Device before, after;
        Context context;
        allocations = 0;
        old(&before, &context, vertexImageBarriers, nonVertexImageBarriers);
        oldAllocations += allocations;
        allocations = 0;
        current(&after, &context, vertexImageBarriers, nonVertexImageBarriers);
        currentAllocations += allocations;
        assert(before.fn.count == after.fn.count && before.fn.calls == after.fn.calls);
        assert(allocations == 0);
    }
    std::printf("barrier_cases=10000 old_vector_allocations=%llu current_vector_allocations=%llu\n",
                static_cast<unsigned long long>(oldAllocations),
                static_cast<unsigned long long>(currentAllocations));
    assert(oldAllocations > 19000 && currentAllocations == 0);
}
