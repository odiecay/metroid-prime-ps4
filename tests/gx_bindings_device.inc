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
        wgpu::Limits limits{};limits.maxImmediateSize=sizeof(aurora::gx::DrawImmediateData);limits.maxSampledTexturesPerShaderStage=16;desc.requiredLimits=&limits;
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
            [&](wgpu::RequestDeviceStatus status, wgpu::Device value, wgpu::StringView message) {
                if (status == wgpu::RequestDeviceStatus::Success) device = std::move(value); else std::fprintf(stderr,"device error: %.*s\n",int(message.length),message.data);
            }), 5000000000);
        assert(device);
    }
    ~Device() {
        for (const auto& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
        assert(errors.empty());
    }
};
