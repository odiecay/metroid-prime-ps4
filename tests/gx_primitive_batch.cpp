// Real decoder/staging differential regression for tiny primitive batching.
// Reuse the existing GPU-object seams and fixture helpers, not a mock decoder.
#define GX_DRAW_PREPARATION_HELPERS_ONLY
#include "gx_draw_preparation.cpp"
#include <ctime>

static unsigned indexedKind(){const char* c=getenv("GX_INDEXED_KIND");return c?std::strtoul(c,nullptr,10):0;}
static GXPrimitive indexedPrimitive(GXPrimitive fallback,unsigned draw){
 switch(indexedKind()){case 1:return GX_QUADS;case 2:return GX_TRIANGLEFAN;case 3:return draw%18?GX_TRIANGLES:GX_QUADS;default:return fallback;}
}
static uint64_t cpuNow(){struct timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
static void canonical(gfx::detail::FramePacket& frame){
 const char* path=getenv("GX_CANONICAL");if(!path)return;
 FILE* f=std::fopen(path,"ab");assert(f);
 auto put=[&](const void* data,size_t size){uint64_t n=size;assert(std::fwrite(&n,sizeof(n),1,f)==1);if(size)assert(std::fwrite(data,1,size,f)==size);};
 for(const auto& pass:frame.renderPasses)for(const auto& c:pass.commands){
  put(&c.type,sizeof(c.type));
  if(c.type==gfx::detail::CommandType::SetViewport){put(&c.data.setViewport,sizeof(c.data.setViewport));continue;}
  if(c.type==gfx::detail::CommandType::SetScissor){put(&c.data.setScissor,sizeof(c.data.setScissor));continue;}
  if(c.type==gfx::detail::CommandType::DebugMarker){put(&c.data.debugMarkerIndex,sizeof(c.data.debugMarkerIndex));continue;}
  assert(c.type==gfx::detail::CommandType::Draw);
  const auto* d=reinterpret_cast<const gx::DrawData*>(c.data.draw.payload.data());
  std::array<uint64_t,15> values{d->pipeline,d->vertRange.offset,d->vertRange.size,d->idxRange.offset,d->idxRange.size,d->uniformRange.offset,d->uniformRange.size,d->vtxCount,d->indexCount,d->instanceCount,d->dstAlpha,d->shadowGroup,d->bindGroups.textureBindGroup,d->immediateData.vtxStart,d->immediateData.serial};
  put(values.data(),sizeof(values));put(&d->immediateData,sizeof(d->immediateData));
 }
 auto bytes=[&](const ByteBuffer& in,const gfx::detail::SpillBuffer& spill){put(in.data(),in.size());put(spill.data.get(),spill.length);};
 bytes(frame.verts,frame.spillVerts);bytes(frame.indices,frame.spillIndices);bytes(frame.storage,frame.spillStorage);bytes(frame.uniforms,frame.spillUniforms);std::fclose(f);
}
static std::vector<u8> tiny_workload(bool materials,bool matrices,GXPrimitive primitive,bool raster=false,unsigned draws=16200){
 std::vector<u8> out;
 const char* countSetting=getenv("GX_INDEXED_VERTICES");if(countSetting){draws=draws*6/std::strtoul(countSetting,nullptr,10)/18*18;}
 for(unsigned draw=0;draw<draws;++draw){
  const char* groupSetting=getenv("GX_INDEXED_GROUP");const unsigned groupSize=groupSetting?std::strtoul(groupSetting,nullptr,10):18;
  const unsigned group=draw/groupSize;
  if(draw%groupSize==0){
   const auto& model=models[group%models.size()];
   array(out,GX_VA_POS,model.positions);array(out,GX_VA_NRM,model.normals);array(out,GX_VA_TEX0,model.uvs);array(out,GX_VA_TEX1,model.uvs);
   auroraCommand(out,GX_AURORA_PORT_DRAW_SERIAL);be(out,group+1,4);
   if(matrices){out.push_back(GX_LOAD_XF_REG);be(out,(3u<<16),4);for(unsigned i=0;i<4;++i)be(out,std::bit_cast<u32>(float(group*4+i)),4);}
   if(raster){out.push_back(GX_LOAD_BP_REG);be(out,(0x41u<<24)|1u|8u|16u|(GX_BL_SRCALPHA<<8)|((group&1?GX_BL_ONE:GX_BL_INVSRCALPHA)<<5),4);}
   if(materials){out.push_back(GX_LOAD_BP_REG);be(out,(0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|((group&1)?GX_CC_RASC:GX_CC_KONST),4);}
  }
  const char* setting=getenv("GX_INDEXED_VERTICES");const unsigned vertices=setting?std::strtoul(setting,nullptr,10):6;
  out.push_back(indexedPrimitive(primitive,draw)|GX_VTXFMT0);be(out,vertices,2);
  for(unsigned v=0;v<vertices;++v){be(out,v,2);be(out,v,2);be(out,v,2);be(out,v,2);}
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
 for(unsigned kind=0;kind<40;++kind){
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
  bool expected=kind==0||kind==1||kind==27||kind==28||kind==29||kind==31||kind==34||kind>=35;
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
  case 35:frame.verts.clear();frame.indices.clear();vertexBytes=indexBytes=0;break;
  case 36:frame.verts.clear();vertexBytes=0;break;
  case 37:frame.indices.clear();indexBytes=0;break;
  case 38:frame.verts.clear();frame.indices.clear();indexBytes=0;break;
  case 39:frame.verts.clear();frame.indices.clear();vertexBytes=0;break;
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
 std::printf("INLINE_MAP_CONTRACT cases=40 successes=%u failures=%u atomic_failure=1 frame_slot_owned=1 no_reallocation=1\n",successes,failures);
}
// Exercise the production vertex mapper independently of index storage.
static void vertex_mapping_contract(gfx::detail::FramePacket& frame){
 auto savedVerts=std::move(frame.verts),savedIndices=std::move(frame.indices);
 const auto savedLimits=gfx::frame_buffer_sizes();
 auto& limits=const_cast<gfx::FrameBufferSizes&>(gfx::frame_buffer_sizes());
 unsigned successes=0,failures=0;
 for(unsigned kind=0;kind<28;++kind){
  tiny_state();
  std::array<uint8_t,192> vertices,indices;vertices.fill(0xa5);indices.fill(0x5a);
  limits=savedLimits;limits.maxVertex=limits.deviceVertex=128;limits.residentVertex=0;
  frame.verts=ByteBuffer{vertices.data()+32,128};frame.indices=ByteBuffer{indices.data()+32,128};
  frame.verts.append_zeroes(16);frame.indices.append_zeroes(8);
  frame.uniforms.clear();frame.storage.clear();frame.renderPasses.clear();
  frame.spillVerts={};frame.spillIndices={};frame.spillUniforms={};frame.spillStorage={};
  const bool active=kind!=18;if(active)gfx::detail::begin_recording(frame,0);
  size_t length=32;
  bool expected=kind==0||kind==1||kind==14||kind==15||kind==17||kind==19||kind==20||kind==21||kind==22||kind==24||kind==25||kind==26;
  switch(kind){
  case 1:limits.residentVertex=4096;break;
  case 3:length=113;break;
  case 4:frame.spillVerts.data=std::make_unique<uint8_t[]>(1);frame.spillVerts.capacity=frame.spillVerts.length=1;break;
  case 5:limits.maxVertex=47;break;
  case 6:limits.deviceVertex=47;break;
  case 7:frame.verts=ByteBuffer{vertices.data()+32,47};frame.verts.append_zeroes(16);break;
  case 8:frame.verts.fill_to_capacity();break;
  case 9:limits.maxVertex=15;break;
  case 10:limits.deviceVertex=15;break;
  case 11:frame.verts=ByteBuffer{nullptr,128};break;
  case 13:limits.residentVertex=uint64_t(UINT32_MAX)-16;break;
  case 14:limits.residentVertex=uint64_t(UINT32_MAX)-16-32;break;
  case 15:limits.maxVertex=limits.deviceVertex=48;break;
  case 16:length=SIZE_MAX;break;
  case 17:length=0;break;
  case 19:frame.spillIndices.data=std::make_unique<uint8_t[]>(1);frame.spillIndices.capacity=frame.spillIndices.length=1;break;
  case 20:frame.indices=ByteBuffer{nullptr,128};break;
  case 21:frame.indices=ByteBuffer{indices.data()+32,0};break;
  case 22:limits.maxIndex=limits.deviceIndex=0;break;
  case 23:limits.residentVertex=4096;break;
  case 24:length=112;break;
  case 25:frame.verts.fill_to_capacity();length=0;break;
  case 26:frame.verts.clear();length=0;break;
  case 27:frame.verts=ByteBuffer{nullptr,128};length=0;break;
  default:break;
  }
  uint64_t expectedEnd=limits.residentVertex+frame.verts.size();
  if(kind==2)++expectedEnd;if(kind==12)expectedEnd=uint64_t(UINT32_MAX)+1;
  if(kind==23)expectedEnd=frame.verts.size();
  const auto vertexBefore=vertices,indexBefore=indices;
  auto* vertexBacking=frame.verts.data();auto* indexBacking=frame.indices.data();
  const auto vertexUsed=frame.verts.size(),indexUsed=frame.indices.size();
  const auto vertexCapacity=frame.verts.capacity(),indexCapacity=frame.indices.capacity();
  const auto vertexSpillLength=frame.spillVerts.length,indexSpillLength=frame.spillIndices.length;
  auto* vertexSpill=frame.spillVerts.data.get();auto* indexSpill=frame.spillIndices.data.get();
  uint8_t* output=reinterpret_cast<uint8_t*>(uintptr_t(1));gfx::Range range{91,92};
  const bool mapped=gfx::map_inline_vertices(expectedEnd,length,output,range);
  if(mapped!=expected)std::fprintf(stderr,"vertex mapping case=%u expected=%d actual=%d\n",kind,expected,mapped);
  assert(mapped==expected);
  assert(frame.verts.data()==vertexBacking&&frame.indices.data()==indexBacking);
  assert(frame.verts.capacity()==vertexCapacity&&frame.indices.capacity()==indexCapacity);
  assert(frame.spillVerts.length==vertexSpillLength&&frame.spillIndices.length==indexSpillLength);
  assert(frame.spillVerts.data.get()==vertexSpill&&frame.spillIndices.data.get()==indexSpill);
  assert(vertices==vertexBefore&&indices==indexBefore);
  assert(frame.indices.size()==indexUsed);
  if(!mapped){
   assert(output==nullptr&&range.offset==0&&range.size==0&&frame.verts.size()==vertexUsed);++failures;
  }else{
   assert(output==vertexBacking+vertexUsed&&range.offset==expectedEnd&&range.size==length);
   assert(frame.verts.size()==vertexUsed+length);
   std::memset(output,0xc3,length);
   for(size_t n=0;n<vertices.size();++n)assert(vertices[n]==(n>=32+vertexUsed&&n<32+vertexUsed+length?0xc3:vertexBefore[n]));
   assert(indices==indexBefore);++successes;
  }
  if(active){gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);}
  output=vertices.data();range={1,2};
  assert(!gfx::map_inline_vertices(0,1,output,range));
  assert(output==nullptr&&range.offset==0&&range.size==0);
  frame.spillVerts={};frame.spillIndices={};frame.spillUniforms={};frame.spillStorage={};
 }
 frame.verts=std::move(savedVerts);frame.indices=std::move(savedIndices);limits=savedLimits;
 frame.renderPasses.clear();frame.uniforms.clear();frame.storage.clear();
 std::printf("VERTEX_MAP_CONTRACT cases=28 successes=%u failures=%u transactional=1 frame_slot_owned=1 no_reallocation=1 index_unchanged=1\n",successes,failures);
}

#ifdef GX_PRIMITIVE_BATCH_SCAN_TESTS
namespace aurora::gx::fifo {
extern uint64_t testTriangleVisits,testTriangleMaps;
bool test_try_batch_triangles(GXVtxFmt,u16,u32,ByteReader&,DrawData*) noexcept;
}

static void triangle_scan_contract(gfx::detail::FramePacket& frame){
 auto savedVerts=std::move(frame.verts),savedIndices=std::move(frame.indices);
 const auto savedLimits=gfx::frame_buffer_sizes();auto& limits=const_cast<gfx::FrameBufferSizes&>(gfx::frame_buffer_sizes());
 const bool enabled=env_flag("MP_GX_PRIMITIVE_BATCH",true)&&env_flag("MP_GX_TRIANGLE_BATCH",true);
 unsigned successes=0,failures=0,zeroScans=0;uint64_t visits=0,maps=0;
 for(unsigned kind=0;kind<15;++kind){
  tiny_state();gx::testing::set_vertex_array_check(false);
  std::array<uint8_t,640> vertexBytes,indexBytes;vertexBytes.fill(0xa5);indexBytes.fill(0x5a);
  limits=savedLimits;limits.maxVertex=limits.deviceVertex=512;limits.residentVertex=0;
  frame.verts=ByteBuffer{vertexBytes.data()+32,512};frame.indices=ByteBuffer{indexBytes.data()+32,512};
  frame.verts.append_zeroes(64);frame.indices.append_zeroes(32);
  frame.renderPasses.clear();frame.uniforms.clear();frame.storage.clear();
  frame.spillVerts={};frame.spillIndices={};frame.spillStorage={};frame.spillUniforms={};
  const bool active=kind!=14;if(active)gfx::detail::begin_recording(frame,0);
  gx::DrawData draw{};draw.vertRange={16,48};draw.vtxCount=6;draw.instanceCount=1;
  switch(kind){
  case 0:frame.spillVerts.data=std::make_unique<uint8_t[]>(1);frame.spillVerts.capacity=frame.spillVerts.length=1;break;
  case 1:frame.verts=ByteBuffer{vertexBytes.data()+32,64};frame.verts.fill_to_capacity();break;
  case 2:limits.deviceVertex=64;break;
  case 3:limits.maxVertex=64;break;
  case 4:(void)frame.verts.append_uninit(1);break;
  case 5:frame.verts=ByteBuffer{nullptr,512};draw.vertRange={0,0};break;
  case 6:draw.vertRange={UINT32_MAX,1};break;
  case 7:limits.residentVertex=1024;draw.vertRange.offset+=1024;break;
  case 8:frame.spillIndices.data=std::make_unique<uint8_t[]>(1);frame.spillIndices.capacity=frame.spillIndices.length=1;break;
  case 9:frame.indices=ByteBuffer{nullptr,0};break;
  case 10:limits.deviceIndex=limits.maxIndex=0;break;
  case 11:limits.deviceVertex=160;break;
  case 12:limits.deviceVertex=112;break;
  case 13:limits.deviceVertex=111;break;
  default:break;
  }
  std::vector<u8> commands;for(unsigned n=0;n<3;++n){
   if(n){commands.push_back(GX_TRIANGLES);be(commands,6,2);}
   for(unsigned byte=0;byte<48;++byte)commands.push_back(u8(n*17+byte));
  }
  ByteReader reader{commands.data(),commands.size()};
  const auto vBefore=vertexBytes,iBefore=indexBytes;
  auto* vPointer=frame.verts.data();auto* iPointer=frame.indices.data();
  const auto vCapacity=frame.verts.capacity(),iCapacity=frame.indices.capacity();
  const auto vUsed=frame.verts.size(),iUsed=frame.indices.size();
  const auto drawBefore=draw;
  gx::fifo::testTriangleVisits=gx::fifo::testTriangleMaps=0;
  const bool result=gx::fifo::test_try_batch_triangles(GX_VTXFMT0,6,8,reader,&draw);
  const bool expected=enabled&&(kind>=7&&kind<=11);
  assert(result==expected);
  const unsigned accepted=kind==11?2:3;
  const unsigned expectedVisits=!enabled||kind<=6||kind==14?0:kind==12?2:kind==13?1:3;
  assert(gx::fifo::testTriangleVisits<=expectedVisits);
  assert(gx::fifo::testTriangleMaps==(expected?1:0));
  if(!expectedVisits)++zeroScans;
  visits+=gx::fifo::testTriangleVisits;maps+=gx::fifo::testTriangleMaps;
  assert(frame.verts.data()==vPointer&&frame.indices.data()==iPointer);
  assert(frame.verts.capacity()==vCapacity&&frame.indices.capacity()==iCapacity);
  assert(frame.indices.size()==iUsed&&indexBytes==iBefore);
  assert(draw.vertRange.offset==drawBefore.vertRange.offset&&draw.indexCount==0&&draw.idxRange.size==0);
  if(!result){
   assert(reader.offset()==0&&frame.verts.size()==vUsed&&vertexBytes==vBefore);
   assert(draw.vertRange.size==drawBefore.vertRange.size&&draw.vtxCount==drawBefore.vtxCount);++failures;
  }else{
   assert(reader.offset()==accepted*48+(accepted-1)*3);
   assert(frame.verts.size()==vUsed+accepted*48&&draw.vertRange.size==drawBefore.vertRange.size+accepted*48);
   assert(draw.vtxCount==drawBefore.vtxCount+accepted*6);
   size_t at=0;for(unsigned n=0;n<accepted;++n){if(n)at+=3;assert(!std::memcmp(vPointer+vUsed+n*48,commands.data()+at,48));at+=48;}
   for(size_t n=0;n<vertexBytes.size();++n)if(n<32+vUsed||n>=32+vUsed+accepted*48)assert(vertexBytes[n]==vBefore[n]);
   ++successes;
  }
  if(active){gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);}
  frame.spillVerts={};frame.spillIndices={};frame.spillStorage={};frame.spillUniforms={};
 }
 frame.verts=std::move(savedVerts);frame.indices=std::move(savedIndices);limits=savedLimits;
 frame.renderPasses.clear();frame.uniforms.clear();frame.storage.clear();
 std::printf("TRIANGLE_SCAN_CONTRACT cases=15 enabled=%d successes=%u failures=%u zero_scan_cases=%u loop_visits=%llu maps=%llu bounded_fallback=1\n",enabled,successes,failures,zeroScans,(unsigned long long)visits,(unsigned long long)maps);
}

#endif

#ifdef GX_INDEXED_BATCH_SCAN_TESTS
#include "gx_indexed_scan_contract.inc"
#endif

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
#ifdef GX_INDEXED_BATCH_SCAN_TESTS
 indexed_scan_contract(frame);
#endif
 inline_mapping_contract(frame);
 vertex_mapping_contract(frame);
#ifdef GX_PRIMITIVE_BATCH_SCAN_TESTS
 triangle_scan_contract(frame);
#endif
 if(const char* malformed=getenv("GX_TINY_MALFORMED")){
  tiny_state();gfx::detail::begin_recording(frame,0);std::vector<u8> bad;
  const GXPrimitive badPrimitive=env_flag("GX_TINY_MALFORMED_TRIANGLES")?GX_TRIANGLES:GX_TRIANGLESTRIP;
  const char* prefixSetting=getenv("GX_TINY_MALFORMED_PREFIX");
  const unsigned prefix=prefixSetting?std::strtoul(prefixSetting,nullptr,10):3;
  for(unsigned n=0;n<prefix;++n){bad.push_back(indexedPrimitive(badPrimitive,n));be(bad,6,2);for(unsigned i=0;i<48;++i)bad.push_back(0);}
  bad.push_back(indexedPrimitive(badPrimitive,prefix));bad.push_back(0);
  if(!std::strcmp(malformed,"payload")){bad.push_back(100);bad.push_back(0);}
  gx::fifo::process(bad.data(),bad.size());std::fprintf(stderr,"MALFORMED_ACCEPTED\n");return 2;
 }


 const char* names[]={"cached_triangles","uniform_triangles","material_uniform_triangles","material_uniform_strips","raster_uniform_triangles","raster_uniform_strips"};
 for(unsigned scenario=0;scenario<6;++scenario){
  const auto commands=tiny_workload(scenario==2||scenario==3,scenario>=1,(scenario==3||scenario==5)?GX_TRIANGLESTRIP:GX_TRIANGLES,scenario>=4);
  std::array<uint64_t,9> samples{};std::array<Timing,MetricCount> totals{};
  uint64_t uniformHash=0,vertexHash=0,indexHash=0;unsigned countedDraws=0,preparationCount=0;
  for(unsigned iteration=0;iteration<14;++iteration){
   tiny_state();gx::fifo::clear_draw_cache();gx::fifo::clear_frame_arrays();
   frame.renderPasses.clear();frame.verts.clear();frame.indices.clear();frame.storage.clear();frame.uniforms.clear();
   gfx::detail::begin_recording(frame,0);
   times={};profile=iteration==13;
   const uint64_t begin=cpuNow();const auto result=gx::fifo::process(commands.data(),commands.size());const uint64_t elapsed=cpuNow()-begin;
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
   const char* verticesSetting=getenv("GX_INDEXED_VERTICES");const unsigned expectedDraws=verticesSetting?16200*6/std::strtoul(verticesSetting,nullptr,10)/18:900;const char* gs=getenv("GX_INDEXED_GROUP");const unsigned grouping=gs?std::strtoul(gs,nullptr,10):18;assert(draws==expectedDraws*18/grouping);countedDraws=draws;preparationCount=expectedDraws*18;
   if(iteration==13){canonical(frame);vertexHash=xxh3_hash_s(frame.verts.data(),frame.verts.size());indexHash=xxh3_hash_s(frame.indices.data(),frame.indices.size());}
   gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);
  }
  std::sort(samples.begin(),samples.end());
  std::printf("DRAW_PREP scenario=%s draws=%u preparations=%u fifo_bytes=%zu median_ms=%.6f min_ms=%.6f max_ms=%.6f storage_bytes=%zu vertex_bytes=%zu index_bytes=%zu uniform_bytes=%zu verified_arrays=%u uniform_fetch_hash=%016llx vertex_hash=%016llx index_hash=%016llx merged_count=%u\n",
    names[scenario],countedDraws,preparationCount,commands.size(),samples[4]/1e6,samples.front()/1e6,samples.back()/1e6,frame.storage.size(),frame.verts.size(),frame.indices.size(),frame.uniforms.size(),countedDraws*3,(unsigned long long)uniformHash,(unsigned long long)vertexHash,(unsigned long long)indexHash,frame.stats.mergedDrawCallCount);
  const char* labels[]={"pipeline_config","shader_info","uniform_pack","array_staging","vertex_staging","index_staging","texture_resolution"};
  for(unsigned i=0;i<MetricCount;++i)std::printf("DRAW_STAGE scenario=%s stage=%s calls=%llu total_ms=%.6f bytes=%llu\n",names[scenario],labels[i],(unsigned long long)totals[i].calls,totals[i].ns/1e6,(unsigned long long)totals[i].bytes);
 }

 if(env_flag("GX_INDEXED_BENCH")){gfx::detail::shutdown_recording();gx::g_gxState={};return 0;}
 const auto savedLimits=gfx::frame_buffer_sizes();
 for(unsigned kind=0;kind<29;++kind){
  auto savedVerts=std::move(frame.verts),savedIndices=std::move(frame.indices);
  std::array<uint8_t,128> tinyVertex{},tinyIndex{};
  auto& limits=const_cast<gfx::FrameBufferSizes&>(gfx::frame_buffer_sizes());limits=savedLimits;
  if(kind==10)frame.verts=ByteBuffer{tinyVertex.data(),tinyVertex.size()};else frame.verts=std::move(savedVerts);
  if(kind==11)frame.indices=ByteBuffer{tinyIndex.data(),tinyIndex.size()};else frame.indices=std::move(savedIndices);
  if(kind==12)limits.maxVertex=386;
  if(kind==13)limits.maxIndex=98;
  if(kind==22){limits.residentVertex=4096;limits.residentIndex=8192;}
  // This CPU fixture has no device for grow_frame_buffer: exhaust both the
  // committed extent and allocation limit to exercise the existing drop path.
  // The direct mapper/scan contracts independently test device-only bounds.
  if(kind==23)limits.maxVertex=limits.deviceVertex=386;
  if(kind==24)limits.maxIndex=limits.deviceIndex=98;
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
  const unsigned countDraws=kind==14?20000:kind==15||kind>=25?6:kind==18?500:80;
  for(unsigned n=0;n<countDraws;++n){
   if((kind==2||kind==18)&&n%3==0)commands.push_back(GX_NOP);
   if((kind==3||kind==18)&&n%2==0){commands.push_back(GX_LOAD_BP_REG);be(commands,(0xc0u<<24)|(GX_CC_ZERO<<12)|(GX_CC_ZERO<<8)|(GX_CC_ZERO<<4)|(n&2?GX_CC_RASC:GX_CC_KONST),4);}
   if(kind==4&&n%2==0){auroraCommand(commands,GX_AURORA_PORT_DRAW_SERIAL);be(commands,n+2,4);}
   if(kind==8&&n%2==0){auroraCommand(commands,GX_AURORA_LOAD_VIEWPORT_RENDER);for(float value:{0.f,0.f,float(1917+n),1080.f,0.f,1.f})be(commands,std::bit_cast<u32>(value),4);}
   if(kind==9&&n%2==0){auroraCommand(commands,GX_AURORA_LOAD_COPY_SRC);for(u32 value:{n,0u,128u,128u})be(commands,value,4);}
   GXPrimitive primitive=env_flag("GX_TINY_TRIANGLE_CASES")?GX_TRIANGLES:GX_TRIANGLESTRIP;
   if(kind==5||kind==18){constexpr GXPrimitive types[]={GX_TRIANGLESTRIP,GX_TRIANGLEFAN,GX_TRIANGLES,GX_QUADS};primitive=types[(n/4)%4];}
   if(indexedKind())primitive=indexedPrimitive(primitive,n);
   const GXVtxFmt fmt=(kind==7||kind==18)&&n%2?GX_VTXFMT1:GX_VTXFMT0;
   unsigned vertices=kind==1?n%13:6;
   if(kind==14)vertices=4;
   if(kind==15)vertices=n==3?21848:6;
   if(kind==16)vertices=n%5+1;
   if(kind==18)vertices=(n*1009u^n*17u)%31;
   if(kind>=25)vertices=n==3?(kind==25?43691:kind==26?43692:kind==27?65534:65535):6;
   commands.push_back(primitive|fmt);be(commands,vertices,2);
   for(unsigned v=0;v<vertices;++v)for(unsigned a=0;a<4;++a)be(commands,v%6,2);
   if(kind==17)boundaries.push_back(commands.size());
  }
  boundaries.push_back(commands.size());
  size_t begin=0;
  for(size_t end:boundaries){if(end==begin)continue;auto result=gx::fifo::process(commands.data()+begin,end-begin);assert(result.bytesProcessed==end-begin&&!result.drawDone);begin=end;}
  gfx::finish();gfx::detail::end_recording();
  canonical(frame);
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
