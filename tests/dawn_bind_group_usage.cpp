#include <webgpu/webgpu_cpp.h>
#include "src/dawn/native/CommandBuffer.h"
#include "src/dawn/native/Commands.h"
#include <set>
static uint64_t probeHits=0,probeMisses=0;
extern "C" void prime_ps4_bind_group_usage_counts(uint64_t hits,uint64_t misses) {probeHits+=hits;probeMisses+=misses;}
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

// The proof build overrides only the cache-enable selector in the actual
// production tracker object. Ordinary builds keep the existing environment flag.
#ifdef PRIME_USAGE_POLICY_PROOF
extern "C" { bool prime_usage_test_reuse = true; }
#endif
static void SetUsageReuseForTesting(bool enabled) {
#ifdef PRIME_USAGE_POLICY_PROOF
    prime_usage_test_reuse = enabled;
#else
    assert(enabled);
#endif
}

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

void benchmark(Device& d, bool verify = false) {
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
    for (unsigned m = 0; m < 256; ++m) {
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
    const char* workload = std::getenv("PRIME_USAGE_WORKLOAD");
    const std::string pattern = workload ? workload : "reuse16";
    const unsigned draws = pattern == "empty" ? 0 : pattern == "cold" ? 256 : 2445;
    const unsigned count = pattern == "reuse48" ? 48 : pattern == "reuse96" ? 96 : pattern == "cold" ? 256 : 16;
    std::set<uintptr_t> identities;
    for (const auto& group : materials) identities.insert(reinterpret_cast<uintptr_t>(group.Get()));
    std::vector<unsigned> sequence;
    std::mt19937 rng(0x108060);
    for(unsigned i=0;i<draws;++i)
        sequence.push_back(pattern=="skew" && rng()%10==0 ? 16+rng()%240 : i%count);
    std::array<std::vector<double>,2> times;
    std::array<uint64_t,2> totalHits{},totalMisses{};
    using Commands = std::vector<std::array<uint64_t,6>>;
    const auto commandsOf = [](const wgpu::CommandBuffer& buffer) {
        Commands commands;
        auto* iter=native::FromAPI(buffer.Get())->GetCommandIteratorForTesting();iter->Reset();
        native::Command type;
        while(iter->NextCommandId(&type)) {
            if(type==native::Command::SetRenderPipeline) {
                auto* c=iter->NextCommand<native::SetRenderPipelineCmd>();
                commands.push_back({uint64_t(type),uintptr_t(c->pipeline.Get())});
            } else if(type==native::Command::SetBindGroup) {
                auto* c=iter->NextCommand<native::SetBindGroupCmd>();
                auto dynamic=c->dynamicOffsetCount==native::BindingIndex{} ? dawn::ityp::span<native::BindingIndex,const uint32_t>{} : iter->NextData<uint32_t>(c->dynamicOffsetCount);
                commands.push_back({uint64_t(type),uint64_t(c->index),uintptr_t(c->group.Get()),uint64_t(c->dynamicOffsetCount),dynamic.empty()?0u:dynamic[native::BindingIndex{}]});
            } else if(type==native::Command::Draw) {
                auto* c=iter->NextCommand<native::DrawCmd>();
                commands.push_back({uint64_t(type),c->vertexCount,c->instanceCount,c->firstVertex,c->firstInstance});
            } else native::SkipCommand(iter,type);
        }
        iter->Reset();return commands;
    };
    for (unsigned frame = 0; frame < 64; ++frame) {
        std::array<Commands,2> recorded;
        std::array<Normalized,2> usage;
        for(unsigned ordered=0;ordered<(verify ? 2u : 1u);++ordered) {
            const unsigned mode=verify ? (ordered ^ (frame&1u)) : 1u;
            SetUsageReuseForTesting(mode == 1);
            probeHits=probeMisses=0;
            const auto start = std::chrono::steady_clock::now();
            auto encoder = d.device.CreateCommandEncoder();
            auto pass = encoder.BeginRenderPass(&passDesc);
            pass.SetPipeline(pipeline);
            for (unsigned i = 0; i < draws; ++i) {
                const uint32_t offset = (i % 8) * 256;
                pass.SetBindGroup(0, uniformGroup, 1, &offset);
                pass.SetBindGroup(1, materials[sequence[i]]);
                pass.Draw(3);
            }
            pass.End();
            auto commands = encoder.Finish();
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            if (frame >= 8) {
                times[mode].push_back(us);totalHits[mode]+=probeHits;totalMisses[mode]+=probeMisses;
            }
            // Compare actual finished command streams and the complete resource
            // usage union outside the recording measurement; submit both.
            recorded[mode]=commandsOf(commands);
            usage[mode]=normalize(native::FromAPI(commands.Get())->GetResourceUsages().renderPasses[native::PassIndex{}]);
            d.device.GetQueue().Submit(1, &commands);
        }
        if (verify) { assert(recorded[0]==recorded[1]);assert(usage[0]==usage[1]); }
    }
    for(unsigned mode=0;mode<2;++mode) {
        if (!verify && mode == 0) continue;
        auto& t=times[mode];std::sort(t.begin(),t.end());
        std::printf("{\"workload\":\"%s\",\"mode\":%u,\"draws\":%u,\"frames\":%zu,\"unique_group_identities\":%zu,\"median_us\":%.3f,\"p95_us\":%.3f,\"hits\":%llu,\"misses\":%llu,\"same_commands_and_usage\":%s}\n",
            pattern.c_str(),mode,draws,t.size(),identities.size(),t[t.size()/2],t[t.size()*95/100],
            (unsigned long long)totalHits[mode],(unsigned long long)totalMisses[mode],verify ? "true" : "null");
    }
}

#ifdef PRIME_USAGE_POLICY_PROOF
void cache_invariants(Device& d) {
    auto uniform=makeBuffer(d,wgpu::BufferUsage::Uniform);
    wgpu::BindGroupLayoutEntry ue{};
    ue.binding=0;ue.visibility=wgpu::ShaderStage::Vertex;
    ue.buffer.type=wgpu::BufferBindingType::Uniform;ue.buffer.minBindingSize=16;
    wgpu::BindGroupLayoutDescriptor ld{};ld.entryCount=1;ld.entries=&ue;
    auto layout=d.device.CreateBindGroupLayout(&ld);
    wgpu::BindGroupEntry be{};be.binding=0;be.buffer=uniform;be.size=16;
    wgpu::BindGroupDescriptor gd{};gd.layout=layout;gd.entryCount=1;gd.entries=&be;
    std::vector<wgpu::BindGroup> groups;
    for(unsigned i=0;i<512;++i) groups.push_back(d.device.CreateBindGroup(&gd));
    SyncScopeUsageTracker reference,extra;
    std::array<native::BindGroupBase*,64> direct{};
    std::array<native::BindGroupBase*,64> model{};
    auto primary=[](auto* p){auto id=reinterpret_cast<uintptr_t>(p);return((id>>4)^(id>>11))%64;};
    auto alternate=[](auto* p){auto id=reinterpret_cast<uintptr_t>(p);return((id>>6)^(id>>17))%64;};
    std::mt19937 rng(0x49641080);
    uint64_t hits=0,referenceHits=0;
    for(unsigned step=0;step<250000;++step) {
        const unsigned phase=(step/1000)%4;
        const unsigned index=phase==0?rng()%16:phase==1?rng()%96:phase==2?rng()%512:step%48;
        auto* group=native::FromAPI(groups[index].Get());
        const size_t p=primary(group),a=alternate(group);
        const bool baseHit=direct[p]==group;
        const bool modelHit=model[p]==group||model[a]==group;
        assert(!baseHit||modelHit);
        direct[p]=group;
        if(model[p]!=group) {
            if(model[a]==group) {
                std::swap(model[p],model[a]);
                if(model[a]&&a!=primary(model[a])&&a!=alternate(model[a]))model[a]=nullptr;
            }
            else {
                if(model[p]&&!model[alternate(model[p])])model[alternate(model[p])]=model[p];
                model[p]=group;
            }
        }
        // Every occupied baseline primary must remain at exactly that slot.
        for(unsigned i=0;i<64;++i)assert(!direct[i]||direct[i]==model[i]);
        const auto beforeBase=reference.GetBindGroupUsageCountsForTesting();
        const auto beforeExtra=extra.GetBindGroupUsageCountsForTesting();
        SetUsageReuseForTesting(false);reference.AddBindGroup(group);
        SetUsageReuseForTesting(true);extra.AddBindGroup(group);
        const auto afterBase=reference.GetBindGroupUsageCountsForTesting();
        const auto afterExtra=extra.GetBindGroupUsageCountsForTesting();
        assert(afterBase[0]-beforeBase[0]==0);
        assert(afterExtra[0]-beforeExtra[0]==unsigned(modelHit));
        referenceHits+=baseHit;hits+=modelHit;
        if(step%997==996) {
            assert(normalize(reference.AcquireSyncScopeUsage())==normalize(extra.AcquireSyncScopeUsage()));
            direct.fill(nullptr);model.fill(nullptr);
            SyncScopeUsageTracker moved(std::move(extra));extra=std::move(moved);
        }
    }
    assert(normalize(reference.AcquireSyncScopeUsage())==normalize(extra.AcquireSyncScopeUsage()));
    std::printf("monotonic_cases=250000 baseline_hits_preserved=1 actual_hit_counts_equal_model=1 baseline_hits=%llu candidate_hits=%llu\n",(unsigned long long)referenceHits,(unsigned long long)hits);
}

#endif

int main(int argc, char** argv) {
    const bool bench = argc > 1 && std::string(argv[1]) == "--bench";
    const bool verify = argc > 1 && std::string(argv[1]) == "--verify";
    Device device(bench || verify);
    if (bench || verify) benchmark(device, verify);
    else {
        correctness(device);
#ifdef PRIME_USAGE_POLICY_PROOF
        cache_invariants(device);
#endif
    }
}
