#include <webgpu/webgpu_cpp.h>
#include "src/dawn/native/BindGroup.h"
#include "src/dawn/native/Buffer.h"
#include "src/dawn/native/PassResourceUsageTracker.h"
#include "src/dawn/native/Texture.h"
#include "src/dawn/native/dawn_platform.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace native = dawn::native;
using native::SyncScopeUsageTracker;

struct Device {
    wgpu::Instance instance;
    wgpu::Device device;
    std::vector<std::string> errors;
    explicit Device(bool benchmark) {
        const std::array features{wgpu::InstanceFeatureName::TimedWaitAny};
        wgpu::InstanceDescriptor id{};
        id.requiredFeatureCount = features.size();
        id.requiredFeatures = features.data();
        instance = wgpu::CreateInstance(&id);
        assert(instance);
        wgpu::RequestAdapterOptions options{};
        options.backendType = wgpu::BackendType::Null;
        wgpu::Adapter adapter;
        instance.WaitAny(instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::RequestAdapterStatus status, wgpu::Adapter value, wgpu::StringView) {
                if (status == wgpu::RequestAdapterStatus::Success) adapter = std::move(value);
            }), 5000000000);
        assert(adapter);
        wgpu::DeviceDescriptor desc{};
        const char* enabled[] = {"skip_validation"};
        wgpu::DawnTogglesDescriptor toggles{};
        if (benchmark) {
            toggles.enabledToggleCount = 1;
            toggles.enabledToggles = enabled;
            desc.nextInChain = &toggles;
        }
        desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message,
                                           std::vector<std::string>* errors) {
            errors->emplace_back(message.data, message.length);
        }, &errors);
        instance.WaitAny(adapter.RequestDevice(&desc, wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::RequestDeviceStatus status, wgpu::Device value, wgpu::StringView) {
                if (status == wgpu::RequestDeviceStatus::Success) device = std::move(value);
            }), 5000000000);
        assert(device);
    }
    ~Device() {
        for (const auto& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
        assert(errors.empty());
    }
};

using Normalized = std::map<std::string, std::array<uint64_t, 2>>;
Normalized normalize(const native::SyncScopeResourceUsage& usage) {
    Normalized out;
    for (unsigned i = 0; i < usage.buffers.size(); ++i) {
        out["b" + std::to_string(reinterpret_cast<uintptr_t>(usage.buffers[i]))] = {
            uint64_t(usage.bufferSyncInfos[i].usage), uint64_t(usage.bufferSyncInfos[i].shaderStages)};
    }
    for (unsigned i = 0; i < usage.textures.size(); ++i) {
        auto* texture = usage.textures[i];
        for (native::Aspect aspect : native::IterateEnumMask(texture->GetFormat().aspects)) {
            for (unsigned layer = 0; layer < texture->GetArrayLayers(); ++layer) {
                for (unsigned mip = 0; mip < texture->GetNumMipLevels(); ++mip) {
                    const auto& info = usage.textureSyncInfos[i].Get(aspect, layer, mip);
                    out["t" + std::to_string(reinterpret_cast<uintptr_t>(texture)) + ":" +
                        std::to_string(unsigned(aspect)) + ":" + std::to_string(layer) + ":" +
                        std::to_string(mip)] = {uint64_t(info.usage), uint64_t(info.shaderStages)};
                }
            }
        }
    }
    for (auto* t : usage.externalTextures) out["e" + std::to_string(reinterpret_cast<uintptr_t>(t))] = {};
    for (auto* t : usage.usedResourceTables) out["r" + std::to_string(reinterpret_cast<uintptr_t>(t))] = {};
    return out;
}

wgpu::Buffer makeBuffer(Device& d, wgpu::BufferUsage usage) {
    wgpu::BufferDescriptor desc{};
    desc.size = 4096;
    desc.usage = usage;
    return d.device.CreateBuffer(&desc);
}

wgpu::Texture makeTexture(Device& d) {
    wgpu::TextureDescriptor desc{};
    desc.size = {64, 64, 3};
    desc.mipLevelCount = 4;
    desc.format = wgpu::TextureFormat::RGBA8Unorm;
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding;
    return d.device.CreateTexture(&desc);
}

void correctness(Device& d) {
    const bool reuse = std::getenv("MP_BIND_GROUP_USAGE_REUSE") == nullptr ||
                       std::string(std::getenv("MP_BIND_GROUP_USAGE_REUSE")) != "0";
    std::vector<wgpu::Buffer> buffers;
    std::vector<wgpu::Texture> textures;
    std::vector<wgpu::BindGroup> groups;
    std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = 16;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Fragment;
    entries[1].buffer.type = wgpu::BufferBindingType::Storage;
    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    entries[3].binding = 3;
    entries[3].visibility = wgpu::ShaderStage::Fragment;
    entries[3].storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
    entries[3].storageTexture.format = wgpu::TextureFormat::RGBA8Unorm;
    entries[3].storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
    wgpu::BindGroupLayoutDescriptor ld{};
    ld.entryCount = entries.size(); ld.entries = entries.data();
    auto layout = d.device.CreateBindGroupLayout(&ld);
    // Far more unique identities than cache slots; every unique buffer must survive collisions.
    for (unsigned i = 0; i < 192; ++i) {
        buffers.push_back(makeBuffer(d, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Storage));
        if (i < 4) textures.push_back(makeTexture(d));
        wgpu::TextureViewDescriptor view{};
        view.dimension = wgpu::TextureViewDimension::e2DArray;
        view.baseMipLevel = i % 3; view.mipLevelCount = 2;
        view.baseArrayLayer = i % 2; view.arrayLayerCount = 2;
        auto sampled = textures[i % 4].CreateView(&view);
        view.dimension = wgpu::TextureViewDimension::e2D;
        view.baseMipLevel = (i + 1) % 4; view.mipLevelCount = 1;
        view.arrayLayerCount = 1;
        auto storage = textures[(i + 1) % textures.size()].CreateView(&view);
        std::array<wgpu::BindGroupEntry, 4> bindings{};
        bindings[0].binding = 0; bindings[0].buffer = buffers.back(); bindings[0].size = 16;
        bindings[1].binding = 1; bindings[1].buffer = buffers.back(); bindings[1].offset = 256;
        bindings[1].size = 256;
        bindings[2].binding = 2; bindings[2].textureView = sampled;
        bindings[3].binding = 3; bindings[3].textureView = storage;
        wgpu::BindGroupDescriptor gd{};
        gd.layout = layout; gd.entryCount = bindings.size(); gd.entries = bindings.data();
        groups.push_back(d.device.CreateBindGroup(&gd));
    }
    SyncScopeUsageTracker actual, reference;
    std::vector<uint64_t> baseReferences;
    for (const auto& group : groups) baseReferences.push_back(native::FromAPI(group.Get())->GetRefCountForTesting());
    std::mt19937 rng(0x108060);
    for (unsigned i = 0; i < 10000; ++i) {
        auto* group = native::FromAPI(groups[i < groups.size() ? i : rng() % groups.size()].Get());
        actual.AddBindGroup(group);
        // Cold tracking of each bind, merged through the existing production union path.
        SyncScopeUsageTracker once;
        once.AddBindGroup(group);
        reference.MergeResourceUsages(once.AcquireSyncScopeUsage());
    }
    const auto counts = actual.GetBindGroupUsageCountsForTesting();
    assert(counts[0] + counts[1] == 10000);
    assert(!reuse || counts[0] > 0);
    uint64_t retained = 0;
    for (unsigned i = 0; i < groups.size(); ++i) {
        retained += native::FromAPI(groups[i].Get())->GetRefCountForTesting() - baseReferences[i];
    }
    assert(retained <= 64 && (reuse || retained == 0));
    assert(normalize(actual.AcquireSyncScopeUsage()) == normalize(reference.AcquireSyncScopeUsage()));
    assert((actual.GetBindGroupUsageCountsForTesting() == std::array<uint64_t, 2>{0, 0}));
    assert(actual.AcquireSyncScopeUsage().buffers.empty());
    auto* one = native::FromAPI(groups[0].Get());
    const auto originalRefs = one->GetRefCountForTesting();
    actual.AddBindGroup(one);
    actual.AddBindGroup(one);
    assert(one->GetRefCountForTesting() == originalRefs + unsigned(reuse));
    SyncScopeUsageTracker moved(std::move(actual));
    assert((actual.GetBindGroupUsageCountsForTesting() == std::array<uint64_t, 2>{0, 0}));
    assert(actual.AcquireSyncScopeUsage().buffers.empty());
    assert(moved.GetBindGroupUsageCountsForTesting()[0] == unsigned(reuse));
    SyncScopeUsageTracker assigned;
    assigned.AddBindGroup(native::FromAPI(groups[1].Get()));
    assigned = std::move(moved);
    assert((moved.GetBindGroupUsageCountsForTesting() == std::array<uint64_t, 2>{0, 0}));
    assigned = std::move(assigned); // explicit valid no-op; keep maps/cache/counters together
    assert(assigned.GetBindGroupUsageCountsForTesting()[0] == unsigned(reuse));
    auto usage = assigned.AcquireSyncScopeUsage();
    assert(!usage.buffers.empty() && one->GetRefCountForTesting() == originalRefs);
    assigned.AddBindGroup(one); // fresh compute-style scope must miss again
    assert((assigned.GetBindGroupUsageCountsForTesting() == std::array<uint64_t, 2>{0, 1}));
    assert(normalize(assigned.AcquireSyncScopeUsage()) == normalize(usage));
    if (reuse) {
        const auto makeIdentityGroup = [&] {
            std::array<wgpu::BindGroupEntry, 4> bindings{};
            bindings[0].binding = 0; bindings[0].buffer = buffers[0]; bindings[0].size = 16;
            bindings[1].binding = 1; bindings[1].buffer = buffers[1]; bindings[1].size = 256;
            wgpu::TextureViewDescriptor view{};
            view.dimension = wgpu::TextureViewDimension::e2DArray;
            bindings[2].binding = 2; bindings[2].textureView = textures[0].CreateView(&view);
            view.dimension = wgpu::TextureViewDimension::e2D;
            view.mipLevelCount = 1; view.arrayLayerCount = 1;
            bindings[3].binding = 3; bindings[3].textureView = textures[1].CreateView(&view);
            wgpu::BindGroupDescriptor gd{};
            gd.layout = layout; gd.entryCount = bindings.size(); gd.entries = bindings.data();
            return d.device.CreateBindGroup(&gd);
        };
        auto ephemeral = makeIdentityGroup();
        auto* retainedIdentity = native::FromAPI(ephemeral.Get());
        SyncScopeUsageTracker owner;
        owner.AddBindGroup(retainedIdentity);
        ephemeral = nullptr; // the cache itself now owns the only group reference
        assert(retainedIdentity->GetRefCountForTesting() > 0);
        std::vector<wgpu::BindGroup> newcomers;
        for (unsigned i = 0; i < 192; ++i) {
            newcomers.push_back(makeIdentityGroup());
            assert(native::FromAPI(newcomers.back().Get()) != retainedIdentity);
        }
        owner.AddBindGroup(retainedIdentity);
        assert((owner.GetBindGroupUsageCountsForTesting() == std::array<uint64_t, 2>{1, 1}));
        owner.AcquireSyncScopeUsage(); // retires the strong identity key
        owner.AddBindGroup(native::FromAPI(newcomers[0].Get()));
        assert((owner.GetBindGroupUsageCountsForTesting() == std::array<uint64_t, 2>{0, 1}));
    }
    std::printf("actual_null_usage_equal=1 resources=192 buffers +4 textures visits=%llu hits=%llu misses=%llu reuse=%d\n",
                (unsigned long long)(counts[0] + counts[1]), (unsigned long long)counts[0],
                (unsigned long long)counts[1], int(reuse));
}

void benchmark(Device& d) {
    auto uniform = makeBuffer(d, wgpu::BufferUsage::Uniform);
    wgpu::BindGroupLayoutEntry ue{};
    ue.binding = 0; ue.visibility = wgpu::ShaderStage::Fragment;
    ue.buffer.type = wgpu::BufferBindingType::Uniform;
    ue.buffer.hasDynamicOffset = true; ue.buffer.minBindingSize = 16;
    wgpu::BindGroupLayoutDescriptor ud{}; ud.entryCount = 1; ud.entries = &ue;
    auto ul = d.device.CreateBindGroupLayout(&ud);
    wgpu::BindGroupEntry ub{}; ub.binding = 0; ub.buffer = uniform; ub.size = 16;
    wgpu::BindGroupDescriptor ugd{}; ugd.layout = ul; ugd.entryCount = 1; ugd.entries = &ub;
    auto uniformGroup = d.device.CreateBindGroup(&ugd);
    std::array<wgpu::BindGroupLayoutEntry, 9> te{};
    for (unsigned i = 0; i < 8; ++i) {
        te[i].binding = i; te[i].visibility = wgpu::ShaderStage::Fragment;
        te[i].texture.sampleType = wgpu::TextureSampleType::Float;
        te[i].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    }
    te[8].binding = 8; te[8].visibility = wgpu::ShaderStage::Fragment;
    te[8].sampler.type = wgpu::SamplerBindingType::Filtering;
    wgpu::BindGroupLayoutDescriptor td{}; td.entryCount = te.size(); td.entries = te.data();
    auto tl = d.device.CreateBindGroupLayout(&td);
    auto texture = makeTexture(d);
    auto sampler = d.device.CreateSampler();
    std::vector<wgpu::BindGroup> materials;
    for (unsigned m = 0; m < 16; ++m) {
        std::array<wgpu::BindGroupEntry, 9> bindings{};
        for (unsigned i = 0; i < 8; ++i) {
            wgpu::TextureViewDescriptor view{};
            view.dimension = wgpu::TextureViewDimension::e2DArray;
            view.baseMipLevel = (m + i) % 3; view.mipLevelCount = 2;
            view.baseArrayLayer = (m + i) % 2; view.arrayLayerCount = 2;
            bindings[i].binding = i; bindings[i].textureView = texture.CreateView(&view);
        }
        bindings[8].binding = 8; bindings[8].sampler = sampler;
        wgpu::BindGroupDescriptor gd{};
        gd.layout = tl; gd.entryCount = bindings.size(); gd.entries = bindings.data();
        materials.push_back(d.device.CreateBindGroup(&gd));
    }
    const std::array layouts{ul, tl};
    wgpu::PipelineLayoutDescriptor pd{}; pd.bindGroupLayoutCount = layouts.size(); pd.bindGroupLayouts = layouts.data();
    auto pipelineLayout = d.device.CreatePipelineLayout(&pd);
    const char* wgsl = R"(
      @group(0) @binding(0) var<uniform> color: vec4f;
      @group(1) @binding(0) var tex: texture_2d_array<f32>;
      @group(1) @binding(8) var samp: sampler;
      @vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {
        let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3)); return vec4f(p[i],0,1);
      }
      @fragment fn fs()->@location(0) vec4f {return textureSample(tex,samp,vec2f(0.5),0)+color;}
    )";
    wgpu::ShaderSourceWGSL code{}; code.code = wgsl;
    wgpu::ShaderModuleDescriptor md{}; md.nextInChain = &code;
    auto module = d.device.CreateShaderModule(&md);
    wgpu::ColorTargetState target{}; target.format = wgpu::TextureFormat::RGBA8Unorm;
    wgpu::FragmentState fragment{}; fragment.module = module; fragment.entryPoint = "fs";
    fragment.targetCount = 1; fragment.targets = &target;
    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout; pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = "vs"; pipelineDesc.fragment = &fragment;
    auto pipeline = d.device.CreateRenderPipeline(&pipelineDesc);
    wgpu::TextureDescriptor rt{}; rt.size = {8, 8, 1}; rt.format = target.format;
    rt.usage = wgpu::TextureUsage::RenderAttachment;
    auto color = d.device.CreateTexture(&rt).CreateView();
    wgpu::RenderPassColorAttachment attachment{};
    attachment.view = color; attachment.loadOp = wgpu::LoadOp::Clear;
    attachment.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1; passDesc.colorAttachments = &attachment;
    std::vector<double> times;
    constexpr unsigned draws = 1800;
    for (unsigned frame = 0; frame < 90; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        auto encoder = d.device.CreateCommandEncoder();
        auto pass = encoder.BeginRenderPass(&passDesc);
        pass.SetPipeline(pipeline);
        for (unsigned i = 0; i < draws; ++i) {
            const uint32_t offset = (i % 8) * 256;
            pass.SetBindGroup(0, uniformGroup, 1, &offset);
            pass.SetBindGroup(1, materials[i % materials.size()]);
            pass.Draw(3);
        }
        pass.End();
        auto commands = encoder.Finish();
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        if (frame >= 10) times.push_back(us);
        d.device.GetQueue().Submit(1, &commands);
    }
    std::sort(times.begin(), times.end());
    std::printf("{\"benchmark\":\"real_dawn_null_record_finish\",\"draws_per_frame\":%u,\"binds_per_frame\":%u,\"frames\":%zu,\"median_us\":%.3f,\"p95_us\":%.3f,\"max_us\":%.3f}\n",
                draws, draws * 2, times.size(), times[times.size() / 2],
                times[times.size() * 95 / 100], times.back());
}

int main(int argc, char** argv) {
    const bool bench = argc > 1 && std::string(argv[1]) == "--bench";
    Device device(bench);
    if (bench) benchmark(device); else correctness(device);
}
