namespace aurora::gx::fifo {
extern uint64_t testIndexedVisits,testIndexedMaps;
bool test_try_batch_indexed(GXPrimitive,GXVtxFmt,u16,u32,ByteReader&,DrawData*) noexcept;
}
static void indexed_scan_contract(gfx::detail::FramePacket& frame){
 auto savedVerts=std::move(frame.verts),savedIndices=std::move(frame.indices);
 const auto savedLimits=gfx::frame_buffer_sizes();auto& limits=const_cast<gfx::FrameBufferSizes&>(gfx::frame_buffer_sizes());
 const bool enabled=env_flag("MP_GX_PRIMITIVE_BATCH",true)&&env_flag("MP_GX_INDEXED_BATCH",true);
 uint64_t visits=0,maps=0;unsigned cases=0,zeroScans=0;
 for(GXPrimitive prim:{GX_QUADS,GX_TRIANGLEFAN,GX_TRIANGLES})for(unsigned kind=0;kind<30;++kind){
  tiny_state();gx::testing::set_vertex_array_check(false);
  std::array<uint8_t,640> vb,ib;vb.fill(0xa5);ib.fill(0x5a);
  limits=savedLimits;limits.maxVertex=limits.deviceVertex=512;limits.maxIndex=limits.deviceIndex=512;limits.residentVertex=limits.residentIndex=0;
  frame.verts=ByteBuffer{vb.data()+32,512};frame.indices=ByteBuffer{ib.data()+32,512};
  frame.verts.append_zeroes(64);frame.indices.append_zeroes(32);
  frame.renderPasses.clear();frame.uniforms.clear();frame.storage.clear();frame.spillVerts={};frame.spillIndices={};frame.spillStorage={};frame.spillUniforms={};
  gx::DrawData draw{};draw.vertRange={16,48};draw.idxRange={16,16};draw.vtxCount=6;draw.indexCount=8;draw.instanceCount=1;
  const size_t oneIndex=(prim==GX_TRIANGLEFAN?12:6)*sizeof(u16);
  u16 firstCount=6;
  switch(kind){
  case 0:frame.spillVerts.data=std::make_unique<uint8_t[]>(1);frame.spillVerts.capacity=frame.spillVerts.length=1;break;
  case 1:frame.spillIndices.data=std::make_unique<uint8_t[]>(1);frame.spillIndices.capacity=frame.spillIndices.length=1;break;
  case 2:frame.verts.fill_to_capacity();draw.vertRange={16,496};break;
  case 3:frame.indices.fill_to_capacity();draw.idxRange={16,496};break;
  case 4:limits.deviceVertex=64;break;case 5:limits.deviceIndex=32;break;
  case 6:limits.maxVertex=64;break;case 7:limits.maxIndex=32;break;
  case 8:++draw.vertRange.size;break;case 9:++draw.idxRange.size;break;
  case 10:frame.verts=ByteBuffer{nullptr,512};draw.vertRange={0,0};break;
  case 11:frame.indices=ByteBuffer{nullptr,512};draw.idxRange={0,0};break;
  case 12:draw.vertRange={UINT32_MAX,1};break;case 13:draw.idxRange={UINT32_MAX,1};break;
  case 14:gx::testing::set_vertex_array_check(true);break;
  case 16:limits.residentVertex=1024;limits.residentIndex=2048;break;
  case 17:limits.residentVertex=1024;limits.residentIndex=2048;draw.vertRange.offset+=1024;draw.idxRange.offset+=2048;break;
  case 18:limits.deviceVertex=111;break;case 19:limits.deviceIndex=32+oneIndex-1;break;
  case 20:limits.deviceVertex=112;break;case 21:limits.deviceVertex=160;break;
  case 22:limits.deviceIndex=32+oneIndex*2;break;
  case 26:draw.vtxCount=65534;break;case 27:firstCount=65535;break;
  case 28:draw.vtxCount=UINT32_MAX-3;break;case 29:draw.vtxCount=UINT32_MAX;break;
  default:break;
  }
  const bool active=kind!=15;if(active)gfx::detail::begin_recording(frame,0);
  std::vector<u8> commands;for(unsigned n=0;n<3;++n){
   if(n){commands.push_back(kind==23?GX_NOP:u8(prim));be(commands,kind==25?1:6,2);}
   for(unsigned byte=0;byte<48;++byte)commands.push_back(u8(n*17+byte));
  }
  if(kind==24)commands.resize(48+3+1);
  ByteReader reader{commands.data(),commands.size()};
  const auto beforeV=vb,beforeI=ib;const auto drawBefore=draw;const auto vUsed=frame.verts.size(),iUsed=frame.indices.size();
  auto* vPtr=frame.verts.data();auto* iPtr=frame.indices.data();const auto vCap=frame.verts.capacity(),iCap=frame.indices.capacity();
  gx::fifo::testIndexedVisits=gx::fifo::testIndexedMaps=0;
  const bool result=gx::fifo::test_try_batch_indexed(prim,GX_VTXFMT0,firstCount,8,reader,&draw);
  const bool expected=enabled&&(kind==17||kind==21||kind==22);
  assert(result==expected);
  const bool noScan=!enabled||(kind<=19&&kind!=17)||kind==23||kind>=26;
  if(noScan){if(gx::fifo::testIndexedVisits)std::fprintf(stderr,"scan prim=%u kind=%u visits=%llu\n",unsigned(prim),kind,(unsigned long long)gx::fifo::testIndexedVisits);assert(gx::fifo::testIndexedVisits==0);++zeroScans;}
  assert(gx::fifo::testIndexedVisits<=3);assert(gx::fifo::testIndexedMaps==unsigned(expected));
  visits+=gx::fifo::testIndexedVisits;maps+=gx::fifo::testIndexedMaps;++cases;
  assert(frame.verts.data()==vPtr&&frame.indices.data()==iPtr&&frame.verts.capacity()==vCap&&frame.indices.capacity()==iCap);
  if(!result){
   assert(reader.offset()==0&&frame.verts.size()==vUsed&&frame.indices.size()==iUsed&&vb==beforeV&&ib==beforeI);
   assert(!std::memcmp(&draw,&drawBefore,sizeof(draw)));
  }else{
   const unsigned accepted=kind==17?3:2;
   assert(reader.offset()==accepted*48+(accepted-1)*3);
   assert(frame.verts.size()==vUsed+accepted*48&&frame.indices.size()==iUsed+accepted*oneIndex);
   assert(draw.vertRange.size==drawBefore.vertRange.size+accepted*48&&draw.idxRange.size==drawBefore.idxRange.size+accepted*oneIndex);
   assert(draw.vtxCount==drawBefore.vtxCount+accepted*6&&draw.indexCount==drawBefore.indexCount+accepted*oneIndex/sizeof(u16));
   for(size_t n=0;n<vb.size();++n)if(n<32+vUsed||n>=32+vUsed+accepted*48)assert(vb[n]==beforeV[n]);
   for(size_t n=0;n<ib.size();++n)if(n<32+iUsed||n>=32+iUsed+accepted*oneIndex)assert(ib[n]==beforeI[n]);
  }
  if(active){gfx::finish();gfx::detail::end_recording();gfx::detail::recycle_frame_commands(frame,0);}
  frame.spillVerts={};frame.spillIndices={};frame.spillStorage={};frame.spillUniforms={};
 }
 gx::testing::set_vertex_array_check(false);frame.verts=std::move(savedVerts);frame.indices=std::move(savedIndices);limits=savedLimits;
 frame.renderPasses.clear();frame.uniforms.clear();frame.storage.clear();
 std::printf("INDEXED_SCAN_CONTRACT cases=%u enabled=%d zero_scans=%u visits=%llu maps=%llu transactional=1 bounded_fallback=1\n",cases,enabled,zeroScans,(unsigned long long)visits,(unsigned long long)maps);
}
