// Frozen v45 generic builder, with the same per-device shell sampler lifecycle fix.
GXBindGroups reference_build_bind_groups(const ShaderInfo& info) noexcept {
  ZoneScoped;

  if (!info.sampledTextures.any() && !info.sampledIndTextures.any() && !info.usesVolFog && !info.shadowReceive &&
      !info.usesLightmap) {
    // Don't bother re-binding anything
    return {};
  }

  // Using C WGPU types instead of C++ wrappers to avoid destructor overhead
  std::array<WGPUBindGroupEntry, kTextureBindings> textureEntries{};
  textureEntries[MaxTextures * 2].binding = MaxTextures * 2;
  textureEntries[MaxTextures * 2].textureView = gfx::probe::cube_view(g_gxState.pbrCube).Get();
  textureEntries[MaxTextures * 2 + 1].binding = MaxTextures * 2 + 1;
  textureEntries[MaxTextures * 2 + 1].sampler = gfx::probe::sampler().Get();
  for (u32 i = 0; i < gfx::probe::VolumeTextures; ++i) {
    textureEntries[MaxTextures * 2 + 2 + i].binding = MaxTextures * 2 + 2 + i;
    textureEntries[MaxTextures * 2 + 2 + i].textureView = gfx::probe::volume_view(g_gxState.pbrVolume, i).Get();
  }
  textureEntries[kLightmapBinding].binding = kLightmapBinding;
  textureEntries[kLightmapBinding].textureView = gfx::probe::lightmap_view(g_gxState.pbrLightmap).Get();
  textureEntries[kBrdfLutBinding].binding = kBrdfLutBinding;
  textureEntries[kBrdfLutBinding].textureView = gfx::probe::brdf_lut_view().Get();
  textureEntries[kVolFogFroxelBinding].binding = kVolFogFroxelBinding;
  textureEntries[kVolFogFroxelBinding].textureView = gfx::volfog::froxel_view().Get();
  textureEntries[kVolFogSamplerBinding].binding = kVolFogSamplerBinding;
  textureEntries[kVolFogSamplerBinding].sampler = gfx::volfog::sampler().Get();
  if (info.shadowReceive && info.actorDecal) {
    textureEntries[kShadowMapBinding].textureView = gfx::actor_shadow::map_view().Get();
    textureEntries[kShadowSamplerBinding].sampler = gfx::actor_shadow::sampler().Get();
  } else if (info.shadowReceive) {
    textureEntries[kShadowMapBinding].textureView = gfx::shadow::map_view().Get();
    textureEntries[kShadowSamplerBinding].sampler = gfx::shadow::sampler().Get();
  }
  for (u32 i = 0; i < MaxTextures; ++i) {
    const auto& tex = info.shellScene && i < g_gxState.shellTextures.size() ? g_gxState.shellTextures[i]
                                                                             : g_gxState.textures[i];
    WGPUBindGroupEntry& textureEntry = textureEntries[i * 2];
    WGPUBindGroupEntry& samplerEntry = textureEntries[i * 2 + 1];
    textureEntry.binding = i * 2;
    samplerEntry.binding = i * 2 + 1;
    if (tex && (info.sampledTextures[i] || info.sampledIndTextures[i])) {
      textureEntry.textureView = tex.ref->sampleTextureView.Get();
      auto samplerDescriptor = tex.get_descriptor();
      // A mod's native maps under PBR tile many times over a floor. Anisotropic filtering keeps
      // them sharp across the view and averages them along it, so each screen column shows the
      // mean of one line through the repeated tile: a fan of streaks towards the horizon that
      // slides as the camera turns. Seen from 4x up, in every map, so they all take the limit.
      static const int pbrAniso = getenv("MP_PBR_ANISO") ? std::clamp(atoi(getenv("MP_PBR_ANISO")), 1, 16) : 2;
      if (g_gxState.pbr && tex.ref->isReplacement && samplerDescriptor.maxAnisotropy > pbrAniso) {
        samplerDescriptor.maxAnisotropy = pbrAniso;
      }
      // Cost tests 8 and 9 (GXSetPBRCostTest) shade in full with cheaper filtering of the
      // mod's maps: 8 without anisotropy, 9 also without blending between mips.
      const u32 costTest = g_gxState.pbr ? g_gxState.pbr - 1u : 0u;
      if (tex.ref->isReplacement && (costTest == 8 || costTest == 9)) {
        samplerDescriptor.maxAnisotropy = 1;
        if (costTest == 9) {
          samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        }
      }
      // Each slot keeps the sampler it took last: slots mostly see the same sampler state, and the cache
      // never drops a sampler before shutdown, so this saves the hash and the lock.
      const auto* samplerKey = reinterpret_cast<const u8*>(&samplerDescriptor) + kSamplerKeyOffset;
      if (!sSlotSamplers[i] || std::memcmp(sSlotSamplerKeys[i].data(), samplerKey, kSamplerKeySize) != 0) {
        sSlotSamplers[i] = gfx::sampler_ref(samplerDescriptor);
        std::memcpy(sSlotSamplerKeys[i].data(), samplerKey, kSamplerKeySize);
      }
      samplerEntry.sampler = sSlotSamplers[i].Get();
    } else {
      textureEntry.textureView = sEmptyTextureView.Get();
      samplerEntry.sampler = sEmptySampler.Get();
    }
  }
  // The shell's colour pass reads the scene copy (CBUF, textureLod 1, bilinear, clamped) in slot 2;
  // without this frame's copy it reads the empty texture, and the caller draws no colour pass.
  wgpu::TextureView shellScene;
  if (info.shellScene) {
    if (!sShellSceneSampler) sShellSceneSampler = gfx::sampler_ref(wgpu::SamplerDescriptor{
        .addressModeU = wgpu::AddressMode::ClampToEdge,
        .addressModeV = wgpu::AddressMode::ClampToEdge,
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
        .mipmapFilter = wgpu::MipmapFilterMode::Linear,
        .maxAnisotropy = 1,
    });
    shellScene = gfx::vfx::scene_copy_view(gfx::scene_hdr_pass_active());
    textureEntries[GX_TEXMAP2 * 2].textureView = shellScene ? shellScene.Get() : sEmptyTextureView.Get();
    textureEntries[GX_TEXMAP2 * 2 + 1].sampler = sShellSceneSampler.Get();
  }
  const WGPUBindGroupDescriptor textureBindGroupDescriptor{
      .label = {"GX Texture Bind Group", WGPU_STRLEN},
      .layout = info.shadowReceive ? sShadowTextureBindGroupLayout.Get() : sTextureBindGroupLayout.Get(),
      .entryCount = texture_binding_count(),
      .entries = textureEntries.data(),
  };
  // Every entry holds only a texture view or a sampler at a fixed binding, so those handles and the layout
  // identify the bind group; hashing them is far cheaper than hashing the whole entries.
  std::array<const void*, kTextureBindings + 1> key{};
  key[0] = textureBindGroupDescriptor.layout;
  for (size_t i = 0; i < textureBindGroupDescriptor.entryCount; ++i) {
    key[i + 1] = textureEntries[i].textureView ? static_cast<const void*>(textureEntries[i].textureView)
                                               : static_cast<const void*>(textureEntries[i].sampler);
  }
  return {
      .textureBindGroup = gfx::bind_group_ref(xxh3_hash(key), textureBindGroupDescriptor),
  };
}
