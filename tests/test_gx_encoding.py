"""Compile the real GX draw/encoding functions against a recording WebGPU seam."""
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
AURORA=ROOT/'game/MetroidPrimePort/extern/aurora/lib'

class GXEncodingTests(unittest.TestCase):
    def test_whole_buffer_indices_and_cached_uniforms_preserve_draws(self):
        encoding=(AURORA/'gfx/encoding.cpp').read_text()
        helpers=encoding.split('void forget_gx_bindings()',1)[1].split('\n#ifdef __ORBIS__',1)[0]
        draw=(AURORA/'gx/pipeline.cpp').read_text().split('void render(const DrawData& data, const wgpu::RenderPassEncoder& pass) {',1)[1].split('\n}\n',1)[0]
        prelude=r'''
#include "gfx/encoding_state.hpp"
#include "gfx/dawn_call_profile.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>
#define AURORA_ASSERT(condition,...) assert(condition)
namespace wgpu {
enum class IndexFormat {Uint16,Uint32};
struct BindGroup {uintptr_t key;void* Get() const {return reinterpret_cast<void*>(key);}};
struct Buffer {
 uintptr_t key;const std::vector<uint16_t>* elements;
 void* Get() const {return reinterpret_cast<void*>(key);}
 uint64_t GetSize() const {return elements->size()*2;}
};
struct Color {double r,g,b,a;};
struct RenderPassEncoder {
 mutable unsigned uniformBinds=0,indexBinds=0,draws=0;
 mutable uint64_t indexOffset=0,indexSize=0;
 mutable Buffer buffer{};
 mutable std::vector<uint16_t> fetched;
 void SetImmediates(unsigned,const void*,size_t) const {}
 void SetBlendConstant(const Color*) const {}
 void SetBindGroup(unsigned index,BindGroup,unsigned=0,const uint32_t* =nullptr) const {if(index==1)++uniformBinds;}
 void SetIndexBuffer(Buffer b,IndexFormat f,uint64_t offset,uint64_t size) const {
  assert(f==IndexFormat::Uint16);buffer=b;indexOffset=offset;indexSize=size;++indexBinds;
 }
 void DrawIndexed(uint32_t count,uint32_t instances,uint32_t first=0) const {
  assert(instances==1 && uint64_t(first+count)*2<=indexSize);++draws;
  for(uint32_t i=0;i<count;++i)fetched.push_back((*buffer.elements)[indexOffset/2+first+i]);
 }
 void Draw(uint32_t,uint32_t) const {++draws;}
};
}
namespace aurora::gfx {
struct Range {uint32_t offset,size;};
struct Resources {wgpu::Buffer indexBuffer;wgpu::BindGroup uniformBindGroup;};
Resources resources;
namespace detail {Resources& resources(){return gfx::resources;}}
bool bind_pipeline(uint64_t,const wgpu::RenderPassEncoder&) {return true;}
void bind_texture_group(uint64_t,const wgpu::RenderPassEncoder&) {}
void forget_texture_group() {}
namespace overdraw {
uint32_t draw_begin(uint64_t,const wgpu::RenderPassEncoder&) {return 0;}
void draw_end(uint32_t,const wgpu::RenderPassEncoder&) {}
}
GXEncodingState g_gxEncodingState;
uint64_t g_gxEncodingCounts[5]{};
void forget_gx_bindings()'''
        data=r'''
}
namespace aurora::gx {
wgpu::BindGroup g_emptyTextureBindGroup{2};
struct DrawData {
 uint64_t pipeline;
 gfx::Range uniformRange,idxRange;
 uint32_t immediateData,vtxCount,indexCount,instanceCount,dstAlpha;
 struct {uint64_t textureBindGroup;}bindGroups;
 bool shadowGroup;
};
void render(const DrawData& data,const wgpu::RenderPassEncoder& pass) {
'''
        checks=r'''
}
}
int main() {
 using namespace aurora;
 std::vector<uint16_t> indices(65536);
 for(unsigned i=0;i<indices.size();++i)indices[i]=uint16_t(i*37+11);
 gfx::resources={{0x1000,&indices},{0x2000}};
 wgpu::RenderPassEncoder pass;
 gx::DrawData d{};d.indexCount=3;d.instanceCount=1;d.dstAlpha=UINT32_MAX;
 d.uniformRange={256,1024};d.idxRange={0,6};
 std::vector<uint16_t> expected;
 for(unsigned i=0;i<2000;++i) {
  d.idxRange.offset=i*6;
  for(unsigned j=0;j<3;++j)expected.push_back(indices[d.idxRange.offset/2+j]);
  gx::render(d,pass);
 }
 assert(pass.fetched==expected && pass.draws==2000);
 assert(pass.indexBinds==1 && pass.uniformBinds==1);
 d.uniformRange.offset=512;gx::render(d,pass);
 assert(pass.indexBinds==1 && pass.uniformBinds==2);
 // Direct UI/custom encoders and new passes invalidate the GX bindings.
 gfx::forget_gx_bindings();gx::render(d,pass);
 assert(pass.indexBinds==2 && pass.uniformBinds==3);
 gfx::resources.indexBuffer.key=0x3000;gx::render(d,pass);
 assert(pass.indexBinds==3);
 gfx::resources.uniformBindGroup.key=0x4000;gx::render(d,pass);
 assert(pass.uniformBinds==4);
 gfx::forget_gx_bindings();d.indexCount=0;gx::render(d,pass);
 assert(pass.indexBinds==3); // A nonindexed draw does not bind an index buffer.
 gfx::GXEncodingState state;
 assert(!state.first_index(0,2,1));state.bind_index(1,16);
 assert(state.first_index(14,2,1)==7);
 assert(!state.first_index(15,1,0)); // misaligned
 assert(!state.first_index(14,2,2)); // declared range too small
 assert(!state.first_index(14,4,1)); // exceeds buffer
 assert(!state.first_index(UINT32_MAX-1,4,1));
 assert(!state.first_index(0,16,UINT32_MAX)); // multiplication must not wrap
 state.reset();assert(!state.has_index(1) && state.bind_uniform(0x4000,512));
 puts("Actual GX draw path: 2000 indexed draws, identical fetched indices, 1 index bind and 1 uniform bind; pass/resource resets preserved");
}
'''
        out=ROOT/'build/gx-encoding-host';out.mkdir(exist_ok=True)
        unit=out/'actual_encoding.cpp';unit.write_text(prelude+helpers+data+draw+checks)
        program=out/'run'
        subprocess.run(['g++-14','-std=c++20','-O2','-fsanitize=address,undefined',
                        '-I'+str(AURORA),str(unit),'-o',str(program)],check=True)
        result=subprocess.run([program],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertIn('identical fetched indices',result.stdout)

if __name__=='__main__':unittest.main()
