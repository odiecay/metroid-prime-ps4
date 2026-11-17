// Run the actual original/current GX setup against recorded command state.
#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using MtxPtr=float (*)[4];
using ushort=unsigned short;
struct Op {std::string name;std::vector<int64_t> args;bool operator==(const Op&)const=default;};
std::vector<Op> ops;
template<class...Args>void emit(const char*name,Args...args){ops.push_back({name,{int64_t(args)...}});}
struct CGX {
#define COMMAND(name) template<class...Args>static void name(Args...args){emit(#name,args...);}
  COMMAND(SetZMode) COMMAND(SetBlendMode) COMMAND(SetNumTexGens) COMMAND(SetNumChans)
  COMMAND(SetTexCoordGen) COMMAND(SetNumTevStages) COMMAND(SetTevOrder)
  COMMAND(SetStandardTevColorAlphaOp) COMMAND(SetTevColorIn) COMMAND(SetTevAlphaIn)
  COMMAND(SetAlphaCompare) COMMAND(SetTevColorOp) COMMAND(SetTevAlphaOp)
  COMMAND(SetTevKColorSel) COMMAND(SetTevKAlphaSel)
#undef COMMAND
  static void SetVtxDescv(GXVtxDescList* rows){
    for(unsigned i=0;i<3;++i)emit("vtx",rows[i].attr,rows[i].type);
  }
  static void SetTevKColor(GXTevKColorID id,GXColor c){emit("kcolor",id,c.r,c.g,c.b,c.a);}
};
void GXLoadTexMtxImm(MtxPtr m,GXTexMtx id,GXTexMtxType type){
  emit("mask-matrix",id,type);for(unsigned i=0;i<8;++i)emit("matrix",std::bit_cast<uint32_t>((&m[0][0])[i]));
}
void GXInitTexObj(GXTexObj*,const void* data,ushort w,ushort h,GXTexFmt format,GXTexWrapMode a,GXTexWrapMode b,GXBool mip){
  emit("mask-texture",w,h,format,a,b,mip);
  const auto* p=static_cast<const unsigned char*>(data);
  for(unsigned i=0;i<32;++i)emit("mask-byte",p[i]);
}
template<class...Args>void GXInitTexObjLOD(GXTexObj*,Args...args){emit("mask-lod",args...);}
void GXLoadTexObj(GXTexObj*,GXTexMapID id){emit("load-map",id);}
struct CTexture {static void InvalidateTexmap(GXTexMapID id){emit("invalidate-map",id);}};
void GXSetColorUpdate(GXBool value){emit("color-update",value);}
void GXSetAlphaUpdate(GXBool value){emit("alpha-update",value);}
void GXInvalidateTexAll(){emit("invalidate-all");}
template<class...Args>void GXSetVtxAttrFmt(Args...args){emit("vtx-format",args...);}
void GXSetTevColorS10(GXTevRegID id,GXColorS10 c){emit("signed-color",id,c.r,c.g,c.b,c.a);}
#include "movie_conversion.inc"
int main(){
  for(bool field:{false,true})for(bool requested:{false,true}){
    ops.clear();OriginalSetup(field,true);const auto full=ops;
    ops.clear();OriginalSetup(field,requested);const auto old=ops;
    ops.clear();MyTHPGXYuv2RgbSetup(field,requested);const auto result=ops;
#ifdef __ORBIS__
    assert(result.size()==full.size()+1);
    assert((result.front()==Op{"SetAlphaCompare",{GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0}}));
    assert(std::vector<Op>(result.begin()+1,result.end())==full);
    for(const auto& op:result)assert(op.name!="mask-texture"&&op.name!="mask-matrix"&&op.name!="load-map");
    if(!requested){bool masked=false;for(const auto& op:old)if(op.name=="mask-texture")masked=true;assert(masked);}
#else
    assert(result==old);
#endif
    // A following GUI/game draw must not inherit the movie's alpha discard.
    ops.clear();MyTHPGXRestore();
    assert((ops.back()==Op{"SetAlphaCompare",{GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0}}));
  }
  std::puts("fields=2 requested_modes=2 yuv_unchanged=1 alpha_restored=1");
}
