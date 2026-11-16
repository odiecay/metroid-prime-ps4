// Real decoder/staging differential regression for tiny primitive batching.
// Reuse the existing GPU-object seams and fixture helpers, not a mock decoder.
#define GX_DRAW_PREPARATION_HELPERS_ONLY
#include "gx_draw_preparation.cpp"

static std::vector<u8> tiny_workload(bool materials,bool matrices,GXPrimitive primitive,bool raster=false,unsigned draws=16200){
 std::vector<u8> out;
 for(unsigned draw=0;draw<draws;++draw){
  const unsigned group=draw/18;
  if(draw%18==0){
   const auto& model=models[group%models.size()];
   array(out,GX_VA_POS,model.positions);array(out,GX_VA_NRM,model.normals);array(out,GX_VA_TEX0,model.uvs);array(out,GX_VA_TEX1,model.uvs);
   auroraCommand(out,GX_AURORA_PORT_DRAW_SERIAL);be(out,group+1,4);
   if(matrices){out.push_back(GX_LOAD_XF_REG);be(out,(3u<<16),4);for(unsigned i=0;i<4;++i)be(out,std::bit_cast<u32>(float(group*4+i)),4);}
   if(raster){out.push_back(GX_LOAD_BP_REG);be(out,(0x41u<<24)|1u|8u|16u|(GX_BL_SRCALPHA<<8)|((group&1?GX_BL_ONE:GX_BL_INVSRCALPHA)<<5),4);}
   if(materials){out.push_back(GX_LOAD_BP_REG);be(out,(0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|((group&1)?GX_CC_RASC:GX_CC_KONST),4);}
  }
  out.push_back(primitive|GX_VTXFMT0);be(out,6,2);
  for(unsigned v=0;v<6;++v){be(out,v,2);be(out,v,2);be(out,v,2);be(out,v,2);}
 }
 return out;
}
static void tiny_state(){
 state();auto& s=gx::g_gxState;
 s.vtxDesc[GX_VA_TEX1]=GX_INDEX16;s.vtxFmts[0].attrs[GX_VA_TEX1]=s.vtxFmts[0].attrs[GX_VA_TEX0];s.arrays[GX_VA_TEX1].stride=8;
}

// Exercise the real recorder with frame-slot memory, not a replacement mapper.
// A rejected joint reservation must not consume either buffer, allocate, spill
// or leave stale output pointers for the decoder to use.
static void inline_mapping_contract(gfx::detail::FramePacket& frame){
 auto savedVerts=std::move(frame.verts),savedIndices=std::move(frame.indices);
 const auto savedLimits=gfx::frame_buffer_sizes();
 auto& limits=const_cast<gfx::FrameBufferSizes&>(gfx::frame_buffer_sizes());
 unsigned successes=0,failures=0;
 for(unsigned kind=0;kind<35;++kind){
  tiny_state();
  std::array<uint8_t,192> vertices,indices;vertices.fill(0xa5);indices.fill(0x5a);
  limits=savedLimits;limits.maxVertex=limits.deviceVertex=128;limits.maxIndex=limits.deviceIndex=128;
  limits.residentVertex=limits.residentIndex=0;
  frame.verts=ByteBuffer{vertices.data()+32,128};frame.indices=ByteBuffer{indices.data()+32,128};
  frame.verts.append_zeroes(16);frame.indices.append_zeroes(8);
  frame.uniforms.clear();frame.storage.clear();frame.renderPasses.clear();
  frame.spillVerts={};frame.spillIndices={};frame.spillUniforms={};frame.spillStorage={};
  bool active=kind!=33;
  if(active)gfx::detail::begin_recording(frame,0);
  size_t vertexBytes=32,indexBytes=24;
  bool expected=kind==0||kind==1||kind==27||kind==28||kind==29||kind==31||kind==34;
  switch(kind){
  case 1:limits.residentVertex=4096;limits.residentIndex=8192;break;
  case 4:vertexBytes=113;break;
  case 5:indexBytes=121;break; // valid vertex span, invalid index: atomic failure
  case 6:frame.spillVerts.data=std::make_unique<uint8_t[]>(1);frame.spillVerts.capacity=frame.spillVerts.length=1;break;
  case 7:frame.spillIndices.data=std::make_unique<uint8_t[]>(1);frame.spillIndices.capacity=frame.spillIndices.length=1;break;
  case 8:limits.maxVertex=47;break;
  case 9:limits.maxIndex=31;break;
  case 10:limits.deviceVertex=47;break;
  case 11:limits.deviceIndex=31;break;
  case 12:frame.verts=ByteBuffer{vertices.data()+32,47};frame.verts.append_zeroes(16);break;
  case 13:frame.indices=ByteBuffer{indices.data()+32,31};frame.indices.append_zeroes(8);break;
  case 14:frame.verts.fill_to_capacity();break;
  case 15:frame.indices.fill_to_capacity();break;
  case 16:limits.deviceVertex=15;break;
  case 17:limits.deviceIndex=7;break;
  case 18:limits.maxVertex=15;break;
  case 19:limits.maxIndex=7;break;
  case 20:frame.verts=ByteBuffer{nullptr,128};break;
  case 21:frame.indices=ByteBuffer{nullptr,128};break;
  case 22:frame.verts=ByteBuffer{nullptr,128};frame.indices=ByteBuffer{nullptr,128};break;
  case 25:limits.residentVertex=uint64_t(UINT32_MAX)-16;break;
  case 26:limits.residentIndex=uint64_t(UINT32_MAX)-8;break;
  case 27:limits.residentVertex=uint64_t(UINT32_MAX)-16-32;break; // exact uint32 end
  case 28:limits.residentIndex=uint64_t(UINT32_MAX)-8-24;break;
  case 29:limits.maxVertex=limits.deviceVertex=48;limits.maxIndex=limits.deviceIndex=32;break;
  case 30:vertexBytes=indexBytes=SIZE_MAX;break;
  case 31:vertexBytes=indexBytes=0;break;
  case 32:limits.residentVertex=1024;limits.residentIndex=2048;break;
  case 34:limits.residentVertex=17;limits.residentIndex=29;vertexBytes=112;indexBytes=120;break;
  default:break;
  }
  uint64_t vertexEnd=limits.residentVertex+frame.verts.size(),indexEnd=limits.residentIndex+frame.indices.size();
  if(kind==2)++vertexEnd;if(kind==3)++indexEnd;
  if(kind==23)vertexEnd=uint64_t(UINT32_MAX)+1;if(kind==24)indexEnd=uint64_t(UINT32_MAX)+1;
  if(kind==32){vertexEnd=frame.verts.size();indexEnd=frame.indices.size();}
  const auto vertexBefore=vertices,indexBefore=indices;
  auto* vertexBacking=frame.verts.data();auto* indexBacking=frame.indices.data();
  const auto vertexUsed=frame.verts.size(),indexUsed=frame.indices.size();
  const auto vertexCapacity=frame.verts.capacity(),indexCapacity=frame.indices.capacity();
  const auto vertexSpillLength=frame.spillVerts.length,indexSpillLength=frame.spillIndices.length;
  auto* vertexSpill=frame.spillVerts.data.get();auto* indexSpill=frame.spillIndices.data.get();
  uint8_t* vertexOut=reinterpret_cast<uint8_t*>(uintptr_t(1));
  uint8_t* indexOut=reinterpret_cast<uint8_t*>(uintptr_t(2));
  gfx::Range vertexRange{91,92},indexRange{93,94};
  const bool mapped=gfx::map_inline_geometry(vertexEnd,indexEnd,vertexBytes,indexBytes,
                                            vertexOut,indexOut,vertexRange,indexRange);
  assert(mapped==expected);
  assert(frame.verts.data()==vertexBacking&&frame.indices.data()==indexBacking);
  assert(frame.verts.capacity()==vertexCapacity&&frame.indices.capacity()==indexCapacity);
  assert(frame.spillVerts.length==vertexSpillLength&&frame.spillIndices.length==indexSpillLength);
  assert(frame.spillVerts.data.get()==vertexSpill&&frame.spillIndices.data.get()==indexSpill);
  assert(vertices==vertexBefore&&indices==indexBefore); // reservation never copies or clears
  if(!mapped){
   assert(vertexOut==nullptr&&indexOut==nullptr);
   assert(vertexRange.offset==0&&vertexRange.size==0&&indexRange.offset==0&&indexRange.size==0);
   assert(frame.verts.size()==vertexUsed&&frame.indices.size()==indexUsed);++failures;
  }else{
   assert(vertexOut==vertexBacking+vertexUsed&&indexOut==indexBacking+indexUsed);
   assert(vertexRange.offset==vertexEnd&&vertexRange.size==vertexBytes);
   assert(indexRange.offset==indexEnd&&indexRange.size==indexBytes);
   assert(frame.verts.size()==vertexUsed+vertexBytes&&frame.indices.size()==indexUsed+indexBytes);
   std::memset(vertexOut,0xc3,vertexBytes);std::memset(indexOut,0xd7,indexBytes);
   for(size_t n=0;n<vertices.size();++n){
    assert(vertices[n]==(n>=32+vertexUsed&&n<32+vertexUsed+vertexBytes?0xc3:vertexBefore[n]));
    assert(indices[n]==(n>=32+indexUsed&&n<32+indexUsed+indexBytes?0xd7:indexBefore[n]));
   }
   ++successes;
  }
  if(active){gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);}
  // An inactive recorder must clear outputs without consulting the old slot.
  vertexOut=indices.data();indexOut=vertices.data();vertexRange={1,2};indexRange={3,4};
  assert(!gfx::map_inline_geometry(0,0,1,1,vertexOut,indexOut,vertexRange,indexRange));
  assert(vertexOut==nullptr&&indexOut==nullptr&&vertexRange.offset==0&&vertexRange.size==0&&indexRange.offset==0&&indexRange.size==0);
  frame.spillVerts={};frame.spillIndices={};frame.spillUniforms={};frame.spillStorage={};
 }
 frame.verts=std::move(savedVerts);frame.indices=std::move(savedIndices);limits=savedLimits;
 frame.renderPasses.clear();frame.uniforms.clear();frame.storage.clear();
 std::printf("INLINE_MAP_CONTRACT cases=35 successes=%u failures=%u atomic_failure=1 frame_slot_owned=1 no_reallocation=1\n",successes,failures);
}
int main(){
 webgpu::g_graphicsConfig.surfaceConfiguration.format=wgpu::TextureFormat::RGBA8Unorm;
 webgpu::g_graphicsConfig.depthFormat=wgpu::TextureFormat::Depth24Plus;
 webgpu::g_graphicsConfig.msaaSamples=1;
 webgpu::g_frameBuffer.size={1917,1080,1};webgpu::g_frameBuffer.format=wgpu::TextureFormat::RGBA8Unorm;
 webgpu::g_depthBuffer.size={1917,1080,1};webgpu::g_depthBuffer.format=wgpu::TextureFormat::Depth24Plus;
 gfx::detail::resources().limits.minUniformBufferOffsetAlignment=256;
 gfx::detail::resources().limits.minStorageBufferOffsetAlignment=256;
 gfx::detail::testing::suppress_render_worker(true);
 gx::testing::set_vertex_array_check(false);
 for(unsigned a=0;a<models.size();++a){
  auto fill=[&](std::vector<u8>& v,size_t n){v.resize(n);for(unsigned i=0;i<n;++i)v[i]=u8(a*31+i*17);};
  fill(models[a].positions,24*1024);fill(models[a].normals,24*1024);fill(models[a].uvs,32*1024);
 }
 gfx::detail::testing::StagingPool staging;gfx::detail::FramePacket frame;staging.attach(frame);
 inline_mapping_contract(frame);
 if(const char* malformed=getenv("GX_TINY_MALFORMED")){
  tiny_state();gfx::detail::begin_recording(frame,0);std::vector<u8> bad;
  for(unsigned n=0;n<2;++n){bad.push_back(GX_TRIANGLESTRIP);be(bad,6,2);for(unsigned i=0;i<48;++i)bad.push_back(0);}
  bad.push_back(GX_TRIANGLESTRIP);bad.push_back(0);
  if(!std::strcmp(malformed,"payload")){bad.push_back(100);bad.push_back(0);}
  gx::fifo::process(bad.data(),bad.size());std::fprintf(stderr,"MALFORMED_ACCEPTED\n");return 2;
 }


 const char* names[]={"cached_triangles","uniform_triangles","material_uniform_triangles","material_uniform_strips","raster_uniform_triangles","raster_uniform_strips"};
 for(unsigned scenario=0;scenario<6;++scenario){
  const auto commands=tiny_workload(scenario==2||scenario==3,scenario>=1,(scenario==3||scenario==5)?GX_TRIANGLESTRIP:GX_TRIANGLES,scenario>=4);
  std::array<uint64_t,9> samples{};std::array<Timing,MetricCount> totals{};
  uint64_t uniformHash=0,vertexHash=0,indexHash=0;
  for(unsigned iteration=0;iteration<14;++iteration){
   tiny_state();gx::fifo::clear_draw_cache();gx::fifo::clear_frame_arrays();
   frame.renderPasses.clear();frame.verts.clear();frame.indices.clear();frame.storage.clear();frame.uniforms.clear();
   gfx::detail::begin_recording(frame,0);
   times={};profile=iteration==13;
   const uint64_t begin=now();const auto result=gx::fifo::process(commands.data(),commands.size());const uint64_t elapsed=now()-begin;
   profile=false;assert(result.bytesProcessed==commands.size()&&!result.drawDone);
   if(iteration>=4&&iteration<13)samples[iteration-4]=elapsed;
   if(iteration==13)totals=times;
   // The actual recorded commands must address byte-identical source arrays.
   unsigned draws=0;for(const auto& pass:frame.renderPasses)for(const auto& cmd:pass.commands)if(cmd.type==gfx::detail::CommandType::Draw){
    const auto* d=reinterpret_cast<const gx::DrawData*>(cmd.data.draw.payload.data());
    if(iteration==13)uniformHash=xxh3_hash_s(frame.uniforms.data()+d->uniformRange.offset,d->uniformRange.size,uniformHash);
    const auto& m=models[draws%models.size()];
    for(auto [attr,bytes]:{std::pair{GX_VA_POS,&m.positions},std::pair{GX_VA_NRM,&m.normals},std::pair{GX_VA_TEX0,&m.uvs}}){
     const auto off=d->immediateData.arrayStart[attr-GX_VA_POS];assert(off+bytes->size()<=frame.storage.size());
     assert(!std::memcmp(frame.storage.data()+off,bytes->data(),bytes->size()));
    }
    ++draws;
   }
   assert(draws==900);
   if(iteration==13){vertexHash=xxh3_hash_s(frame.verts.data(),frame.verts.size());indexHash=xxh3_hash_s(frame.indices.data(),frame.indices.size());}
   gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);
  }
  std::sort(samples.begin(),samples.end());
  std::printf("DRAW_PREP scenario=%s draws=900 preparations=16200 fifo_bytes=%zu median_ms=%.6f min_ms=%.6f max_ms=%.6f storage_bytes=%zu vertex_bytes=%zu index_bytes=%zu uniform_bytes=%zu verified_arrays=2700 uniform_fetch_hash=%016llx vertex_hash=%016llx index_hash=%016llx merged_count=%u\n",
    names[scenario],commands.size(),samples[4]/1e6,samples.front()/1e6,samples.back()/1e6,frame.storage.size(),frame.verts.size(),frame.indices.size(),frame.uniforms.size(),(unsigned long long)uniformHash,(unsigned long long)vertexHash,(unsigned long long)indexHash,frame.stats.mergedDrawCallCount);
  const char* labels[]={"pipeline_config","shader_info","uniform_pack","array_staging","vertex_staging","index_staging","texture_resolution"};
  for(unsigned i=0;i<MetricCount;++i)std::printf("DRAW_STAGE scenario=%s stage=%s calls=%llu total_ms=%.6f bytes=%llu\n",names[scenario],labels[i],(unsigned long long)totals[i].calls,totals[i].ns/1e6,(unsigned long long)totals[i].bytes);
 }

 const auto savedLimits=gfx::frame_buffer_sizes();
 for(unsigned kind=0;kind<22;++kind){
  auto savedVerts=std::move(frame.verts),savedIndices=std::move(frame.indices);
  std::array<uint8_t,128> tinyVertex{},tinyIndex{};
  auto& limits=const_cast<gfx::FrameBufferSizes&>(gfx::frame_buffer_sizes());limits=savedLimits;
  if(kind==10)frame.verts=ByteBuffer{tinyVertex.data(),tinyVertex.size()};else frame.verts=std::move(savedVerts);
  if(kind==11)frame.indices=ByteBuffer{tinyIndex.data(),tinyIndex.size()};else frame.indices=std::move(savedIndices);
  if(kind==12)limits.maxVertex=386;
  if(kind==13)limits.maxIndex=98;
  tiny_state();gx::g_gxState.vtxFmts[1]=gx::g_gxState.vtxFmts[0];
  if(kind==19)gx::g_gxState.shadowCasterOnly=true;
  if(kind==20){gx::g_gxState.depthPrepass=1;gx::g_gxState.depthCompare=false;}
  if(kind==21)gx::g_gxState.actorLayer=0;
  gx::testing::set_vertex_array_check(kind==6);
  gx::fifo::clear_draw_cache();gx::fifo::clear_frame_arrays();
  frame.renderPasses.clear();frame.verts.clear();frame.indices.clear();frame.storage.clear();frame.uniforms.clear();
  frame.spillVerts={};frame.spillIndices={};frame.spillUniforms={};frame.spillStorage={};
  gfx::detail::begin_recording(frame,0);
  std::vector<u8> commands;
  array(commands,GX_VA_POS,models[0].positions);array(commands,GX_VA_NRM,models[0].normals);
  array(commands,GX_VA_TEX0,models[0].uvs);array(commands,GX_VA_TEX1,models[0].uvs);
  std::vector<size_t> boundaries;
  const unsigned countDraws=kind==14?20000:kind==15?6:kind==18?500:80;
  for(unsigned n=0;n<countDraws;++n){
   if((kind==2||kind==18)&&n%3==0)commands.push_back(GX_NOP);
   if((kind==3||kind==18)&&n%2==0){commands.push_back(GX_LOAD_BP_REG);be(commands,(0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|(n&2?GX_CC_RASC:GX_CC_KONST),4);}
   if(kind==4&&n%2==0){auroraCommand(commands,GX_AURORA_PORT_DRAW_SERIAL);be(commands,n+2,4);}
   if(kind==8&&n%2==0){auroraCommand(commands,GX_AURORA_LOAD_VIEWPORT_RENDER);for(float value:{0.f,0.f,float(1917+n),1080.f,0.f,1.f})be(commands,std::bit_cast<u32>(value),4);}
   if(kind==9&&n%2==0){auroraCommand(commands,GX_AURORA_LOAD_COPY_SRC);for(u32 value:{n,0u,128u,128u})be(commands,value,4);}
   GXPrimitive primitive=GX_TRIANGLESTRIP;
   if(kind==5||kind==18){constexpr GXPrimitive types[]={GX_TRIANGLESTRIP,GX_TRIANGLEFAN,GX_TRIANGLES,GX_QUADS};primitive=types[(n/4)%4];}
   const GXVtxFmt fmt=(kind==7||kind==18)&&n%2?GX_VTXFMT1:GX_VTXFMT0;
   unsigned vertices=kind==1?n%13:6;
   if(kind==14)vertices=4;
   if(kind==15)vertices=n==3?21848:6;
   if(kind==16)vertices=n%5+1;
   if(kind==18)vertices=(n*1009u^n*17u)%31;
   commands.push_back(primitive|fmt);be(commands,vertices,2);
   for(unsigned v=0;v<vertices;++v)for(unsigned a=0;a<4;++a)be(commands,v%6,2);
   if(kind==17)boundaries.push_back(commands.size());
  }
  boundaries.push_back(commands.size());
  size_t begin=0;
  for(size_t end:boundaries){if(end==begin)continue;auto result=gx::fifo::process(commands.data()+begin,end-begin);assert(result.bytesProcessed==end-begin&&!result.drawDone);begin=end;}
  gfx::finish();gfx::detail::end_recording();
  uint64_t signature=0;unsigned draws=0,commandsCount=0;
  for(const auto& pass:frame.renderPasses)for(const auto& c:pass.commands){
   ++commandsCount;signature=xxh3_hash_s(&c.type,sizeof(c.type),signature);
   if(c.type!=gfx::detail::CommandType::Draw)continue;
   const auto* d=reinterpret_cast<const gx::DrawData*>(c.data.draw.payload.data());
   std::array<uint64_t,15> values{d->pipeline,d->vertRange.offset,d->vertRange.size,d->idxRange.offset,d->idxRange.size,d->uniformRange.offset,d->uniformRange.size,d->vtxCount,d->indexCount,d->instanceCount,d->dstAlpha,d->shadowGroup,d->bindGroups.textureBindGroup,d->immediateData.vtxStart,d->immediateData.serial};
   signature=xxh3_hash_s(values.data(),sizeof(values),signature);signature=xxh3_hash_s(&d->immediateData,sizeof(d->immediateData),signature);++draws;
  }
  auto hash=[](const ByteBuffer& bytes,const gfx::detail::SpillBuffer& spill){auto h=xxh3_hash_s(bytes.data(),bytes.size());return spill.length?xxh3_hash_s(spill.data.get(),spill.length,h):h;};
  std::printf("ADVERSARIAL case=%u fifo=%zu draws=%u commands=%u command_hash=%016llx vertices=%zu vertex_hash=%016llx indices=%zu index_hash=%016llx uniforms=%zu uniform_hash=%016llx merged_count=%u\n",kind,commands.size(),draws,commandsCount,(unsigned long long)signature,gfx::detail::logical_size(frame.verts,frame.spillVerts),(unsigned long long)hash(frame.verts,frame.spillVerts),gfx::detail::logical_size(frame.indices,frame.spillIndices),(unsigned long long)hash(frame.indices,frame.spillIndices),gfx::detail::logical_size(frame.uniforms,frame.spillUniforms),(unsigned long long)hash(frame.uniforms,frame.spillUniforms),frame.stats.mergedDrawCallCount);
  gfx::detail::recycle_frame_commands(frame,0);
  frame.spillVerts={};frame.spillIndices={};frame.spillUniforms={};frame.spillStorage={};
  if(kind==10)frame.verts=std::move(savedVerts);if(kind==11)frame.indices=std::move(savedIndices);
  limits=savedLimits;
 }
 gx::testing::set_vertex_array_check(false);
 gfx::detail::shutdown_recording();gx::g_gxState={};
}
