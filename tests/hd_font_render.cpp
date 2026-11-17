// Production font/layout/render bodies run against a recording GX boundary.
// Device submission and asset tokens are fixtures; no game assets are embedded.
#include "port_hd_font.h"
#include "port_env.h"
#include "port_strings.h"
#include "port_utf8_path.h"
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

using uint = unsigned; using uchar = unsigned char; using schar = signed char; using ushort = unsigned short;
using CAssetId = unsigned;
namespace {
int sdf = 0, loads = 0, destroys = 0, invalidations = 0, setups = 0, palettes = 0;
int swaps[2] = {}; bool original = false; std::string fontPath;
const void* atlas = nullptr; size_t atlasBytes = 0;
GXColor regs[2];
struct Draw { int sdf; std::array<float,12> pos{}; GXColor fill{}, outline{}; };
std::vector<Draw> draws; size_t vertex;
bool Close(float a,float b) { return std::abs(a-b)<0.001f; }
}
namespace PortDebug { bool OriginalExperience() { return original; } }
namespace PortMods { std::string FontPath() { return fontPath; } }
namespace PortLog { template<class... T> void Write(const char*,T...) {} }
void GXInitTexObj(GXTexObj*,void* p,u16 w,u16 h,GXTexFmt f,int,int,int) {
  assert(f == GX_TF_R8_PC); ++loads; atlas=p; atlasBytes=size_t(w)*h;
}
void GXInitTexObjLOD(GXTexObj*,int a,int b,float,float,float,int,int,int) { assert(a==GX_LINEAR && b==GX_LINEAR); }
void GXLoadTexObj(GXTexObj*,int) { assert(atlas && atlasBytes); (void)*static_cast<const u8*>(atlas); }
void GXDestroyTexObj(GXTexObj*) { assert(atlas); ++destroys; atlas=nullptr; atlasBytes=0; }
void GXSetSDF(int value) { sdf=value; }
void GXSetTevSwapMode(int stage,int,int value) { swaps[stage]=value; }
void GXSetTevColor(int reg,GXColor c) { regs[reg-GX_TEVREG0]=c; }
void GXPosition3f32(float x,float y,float z) { assert(vertex<4);draws.back().pos[vertex*3]=x;draws.back().pos[vertex*3+1]=y;draws.back().pos[vertex*3+2]=z;++vertex; }
void GXTexCoord2f32(float u,float v) { assert(std::isfinite(u)&&std::isfinite(v)); }
struct CTexture {
 enum {kCM_Clamp}; static void InvalidateTexmap(int) {++invalidations;}
 void Load(int,int) {assert(sdf==0);}
};
struct CGX {
 template<class... T> static void SetTevColorIn(T...) {}
 template<class... T> static void SetTevAlphaIn(T...) {}
 template<class... T> static void SetStandardTevColorAlphaOp(T...) {}
 template<class... T> static void SetTevDirect(T...) {}
 template<class... T> static void SetTevOrder(T...) {}
 template<class... T> static void SetVtxDescv(T...) {}
 template<class... T> static void SetNumChans(T...) {}
 template<class... T> static void SetNumTexGens(T...) {}
 template<class... T> static void SetNumTevStages(T...) {}
 template<class... T> static void SetNumIndStages(T...) {}
 template<class... T> static void SetTexCoordGen(T...) {}
 template<class... T> static void SetTevKAlphaSel(T...) {}
 template<class... T> static void SetTevKColorSel(T...) {}
 static void SetTevKColor(int,GXColor c) {regs[0]=c;}
 static void Begin(int,int,int count) {assert(count==4);vertex=0;draws.push_back({sdf,{},regs[0],regs[1]});}
 static void End() {assert(vertex==4);}
};
class CGraphicsPalette {
 std::array<u8,8> data{0,0,0xfc,0,0x35,0xa7,0,0};
public:
 int GetTlutFmt() const {return GX_TL_RGB5A3;}
 const ushort* GetPaletteData() const {return reinterpret_cast<const ushort*>(data.data());}
 void Load() const {++palettes;}
};
class CInputStream {
 const u8* data; size_t size,at=0;
public:
 CInputStream(const void* p,size_t n):data(static_cast<const u8*>(p)),size(n) {}
 template<class T> T Get() {
  assert(at+sizeof(T)<=size);uint32_t v=0;
  for(size_t i=0;i<sizeof(T);++i)v=(v<<8)|data[at++];
  if constexpr(std::is_same_v<T,float>) {float f;std::memcpy(&f,&v,4);return f;} else return T(v);
 }
 int ReadInt32(){return Get<int>();} float ReadFloat(){return Get<float>();}
 bool ReadBool(){return Get<u8>()!=0;} char ReadChar(){return Get<char>();} short ReadShort(){return Get<short>();}
 size_t GetReadPosition() const{return at;}
};
struct CMemoryInStream:CInputStream {enum {kOS_NotOwned}; CMemoryInStream(const void* p,size_t n,int):CInputStream(p,n) {}};
namespace rstl {
 template<class A,class B> using pair=std::pair<A,B>;
 template<class T> using vector=std::vector<T>;
 template<class T> void sort_by_key(T& t) {std::sort(t.begin(),t.end(),[](const auto&a,const auto&b){return a.first<b.first;});}
 template<class T,class K> auto find_by_key(const T& t,K k) {return std::find_if(t.begin(),t.end(),[&](const auto&p){return p.first==k;});}
 struct string:std::string {explicit string(CInputStream& in) {for(char c;(c=in.ReadChar())!=0;)push_back(c);}};
}
#include "actual_font_types.inc"
template<class T> struct TToken {
 T* p=nullptr; bool loaded=true; TToken()=default; TToken(T* p):p(p) {}
 bool IsLoaded()const{return p&&loaded;} T* operator->()const{return p;} T* operator*()const{return p;} void Lock(){}
};
struct SObjectTag {SObjectTag(int,unsigned){}};
struct IObjectStore {TToken<CTexture> GetObj(SObjectTag){return {};}};
enum {kTD_Horizontal};
struct CDrawStringOptions {
 int GetExtraCharacterSpacing()const{return 1;} int GetTextDirection()const{return kTD_Horizontal;}
 unsigned GetPaletteEntry(int)const{return 0xffffffff;}
};
class CTextRenderBuffer;
class CRasterFont {
public:
 bool x0_initialized=false;int x4_monoWidth=0,x8_monoHeight=0;
 rstl::vector<rstl::pair<wchar_t,CGlyph>> xc_glyphs;rstl::vector<CKernPair> x1c_kerning;
 EFontMode x2c_mode=kFM_OneLayer;std::optional<CFontInfo> x30_fontInfo;
 struct OptionalToken:std::optional<TToken<CTexture>> {using std::optional<TToken<CTexture>>::operator=;bool valid()const{return has_value();}} x80_texture;
 int x8c_baseline=0,x90_lineMargin=0,mPortLayerMode=-1;
 std::vector<std::pair<wchar_t,int>> mPortLayers;
 CRasterFont(CInputStream&,IObjectStore*);EFontMode GetMode()const;
 const char* PortGetName()const{return x30_fontInfo?x30_fontInfo->GetName():"";}
 const CGlyph* InternalGetGlyph(wchar_t)const;const CGlyph* GetGlyph(wchar_t c)const{return InternalGetGlyph(c);}
 bool HasGlyph(wchar_t c)const{return GetGlyph(c);}
 void PortAddStandIns(){} void PortUnpackLayers(){}
 void SetupRenderState(){assert(sdf==0&&swaps[0]==GX_TEV_SWAP0);++setups;}
 void GetSize(const CDrawStringOptions&,int&,int&,const wchar_t*,int)const;
 static int KernLookup(const rstl::vector<CKernPair>&,int,int);
 void SinglePassDrawString(const CDrawStringOptions&,int,int,int&,int&,CTextRenderBuffer*,const wchar_t*,int)const;
};
struct CColor {
 unsigned v;explicit CColor(unsigned v):v(v){}
 GXColor GetGXColor()const{return {u8(v>>24),u8(v>>16),u8(v>>8),u8(v)};}
 static CColor Modulate(CColor a,CColor b){auto x=a.GetGXColor(),y=b.GetGXColor();return CColor((unsigned(x.r*y.r/255)<<24)|(unsigned(x.g*y.g/255)<<16)|(unsigned(x.b*y.b/255)<<8)|unsigned(x.a*y.a/255));}
};
struct CVector2i {int x,y;CVector2i(int x,int y):x(x),y(y){}};
struct Scale {float GetX()const{return 1;}float GetY()const{return 1;}};
struct CFontImageDef {
 std::vector<TToken<CTexture>> images;float GetFps()const{return 1;}
 const auto& GetImages()const{return images;} short GetMonoWidth()const{return 12;}short GetMonoHeight()const{return 10;}
 Scale GetScale()const{return {};}
};
class CTextRenderBuffer {
public:
 enum ECmd {kC_CharacterRender,kC_ImageRender,kC_FontChange,kC_PaletteChange};
 std::vector<TToken<CRasterFont>> x4_fonts;std::vector<CFontImageDef> x14_images;
 std::vector<u8> x34_bytecode;unsigned x44_blobSize=0;
 mutable int x4c_activeFont=-1,x4d_activePalette=-1,x4e_queuedFont=-1,x4f_queuedPalette=-1;
 std::vector<std::shared_ptr<CGraphicsPalette>> x50_palettes;
 void put(unsigned n,size_t size){while(size)x34_bytecode.push_back(u8(n>>(8*--size)));x44_blobSize=x34_bytecode.size();}
 void AddCharacter(CVector2i p,short c,uint color){put(kC_CharacterRender,1);put(p.x,2);put(p.y,2);put(c,2);put(color,4);}
 void Font(int i){put(kC_FontChange,1);put(i,1);} void Palette(int i){put(kC_PaletteChange,1);put(i,1);}
 void Image(){put(kC_ImageRender,1);put(1,2);put(2,2);put(0,1);put(0xffffffff,4);}
 void Render(const CColor&,float)const;
};
#include "actual_raster_layout.inc"
#include "actual_hd_font.inc"
#include "actual_text_render.inc"

// The actual R8 upload/swizzle gate, with a minimal WebGPU capability recorder.
namespace wgpu {
 enum class ComponentSwizzle {R,G,B,A};
 enum class TextureFormat {R8Unorm,RG8Unorm,RGBA8Unorm,R16Sint,BC1RGBAUnorm};
 struct TextureComponentSwizzleDescriptor {struct {ComponentSwizzle r{},g{},b{},a{};}swizzle;};
}
namespace webgpu {bool g_textureComponentSwizzleSupported=false,g_bcTexturesSupported=false;}
#include "actual_texture_gate.inc"
std::string SdfShader(int edge) {
 struct {int sdf;} config{edge};std::string fragmentFnPre;int i=0;
#include "actual_sdf_shader.inc"
 return fragmentFnPre;
}
std::vector<u8> Read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);assert(f);return port::ReadAll(f);}

int main(int argc,char**argv) {
 assert(argc==3);unsetenv("MP_HD_FONT");assert(PortHdFont::Enabled());
 fontPath=argv[1];const std::string validFontPath=fontPath;const auto bytes=Read(fontPath);
 PortHdFont::Font asset;std::string error;assert(PortHdFont::ReadFont(bytes.data(),bytes.size(),asset,error));
 assert(asset.width==416&&asset.height==400&&asset.glyphs.size()==195&&asset.padding==4.f);
 for(int c=32;c<127;++c)assert(asset.Find(c));
 assert(asset.Find('H')&&asset.Find('0')&&!asset.Find(0x3042));
 std::vector<std::unique_ptr<CRasterFont>> fonts;
 for(const auto&p:std::filesystem::directory_iterator(argv[2]))if(p.path().extension()==".FONT") {
  auto raw=Read(p.path());CInputStream in(raw.data(),raw.size());auto f=std::make_unique<CRasterFont>(in,nullptr);
  assert(f->x0_initialized&&PortHdFont::SameTypeface(*f));fonts.push_back(std::move(f));
 }
 assert(fonts.size()>=4);int glyphCount=0,layoutCount=0;
 for(auto&f:fonts) {
  const CGlyph* ref=f->GetGlyph('H');if(!ref)ref=f->GetGlyph('0');assert(ref);
  PortHdFont::Fit fit;const bool outline=f->GetMode()==kFM_OneLayerOutline;
  assert(PortHdFont::FitFont(asset,ref->GetCellHeight(),ref->GetBaseLine(),outline,fit));
  assert(fit.edge==(outline?fit.edge:128));if(outline)assert(fit.edge<128&&fit.edge>=16);
  if(const CGlyph* h=f->GetGlyph('H')) {
   const auto box=PortHdFont::GlyphBox(*asset.Find('H'),fit,0,0,h->GetCellWidth(),h->GetBaseLine());
   const float border=outline?1.f:0.f;
   assert(Close(box.top+asset.padding*fit.scale,border));
   assert(Close(box.bottom-asset.padding*fit.scale,h->GetCellHeight()-border));
  }
  printf("font=%s cell=%dx%d baseline=%d edge=%d ",f->PortGetName(),ref->GetCellWidth(),ref->GetCellHeight(),ref->GetBaseLine(),fit.edge);
  auto palette=std::make_shared<CGraphicsPalette>();
  CTextRenderBuffer b;b.x4_fonts.emplace_back(f.get());b.x50_palettes.push_back(palette);
  b.Font(0);b.Palette(0);CDrawStringOptions options;int x,y,w,h;
  f->SinglePassDrawString(options,15,40,x,y,&b,L"AVATAR SAVE DATA 0123456789",-1);
  f->GetSize(options,w,h,L"AVATAR SAVE DATA 0123456789",-1);assert(x-15==w&&y==40);
  auto commands=b.x34_bytecode;PortHdFont::SetEnabled(false);draws.clear();b.Render(CColor(0x80ff4080),0);auto bitmap=draws;
  PortHdFont::SetEnabled(true);draws.clear();b.Render(CColor(0x80ff4080),0);assert(draws.size()==bitmap.size());
  assert(b.x34_bytecode==commands&&sdf==0&&swaps[0]==GX_TEV_SWAP0);assert(!draws.empty());
  for(const auto&d:draws)assert(d.sdf==fit.edge);
  auto color=draws.front().fill;assert(color.r==128&&color.g==0&&color.b==0&&color.a==128);
  auto border=draws.front().outline;assert(border.r==42&&border.g==170&&border.b==29&&border.a==54);
  for(const auto&[chr,cell]:f->xc_glyphs)if(asset.Find(chr)) {
   assert(PortHdFont::Begin(*f));draws.clear();assert(PortHdFont::DrawGlyph(*f,palette.get(),chr,100,50,{255,255,255,255}));
   const auto box=PortHdFont::GlyphBox(*asset.Find(chr),fit,100,50,cell.GetCellWidth(),cell.GetBaseLine());
   assert(Close(draws[0].pos[0],box.left)&&Close(draws[0].pos[2],box.top));
   assert(Close(draws[0].pos[9],box.right)&&Close(draws[0].pos[11],box.bottom));
   assert(Close((box.left+box.right)*.5f,100+cell.GetCellWidth()*.5f));
   PortHdFont::End();++glyphCount;
  }
  // A disc glyph absent from the SDF falls back for the rest of this run.
  f->xc_glyphs.push_back({0x3042,*ref});rstl::sort_by_key(f->xc_glyphs);
  CTextRenderBuffer mixed;mixed.x4_fonts=b.x4_fonts;mixed.x50_palettes=b.x50_palettes;
  CTexture image;mixed.x14_images.push_back({{TToken<CTexture>(&image)}});
  mixed.Font(0);mixed.Palette(0);mixed.AddCharacter({0,0},'0',0xffffffff);
  mixed.AddCharacter({20,0},0x3042,0xffffffff);mixed.AddCharacter({40,0},'0',0xffffffff);
  mixed.Image();mixed.AddCharacter({60,0},'0',0xffffffff);
  for(int repeat=0;repeat<32;++repeat) {
   draws.clear();mixed.Render(CColor(0xffffffff),0);assert(draws.size()==5);
   assert(draws[0].sdf==fit.edge&&draws[1].sdf==0&&draws[2].sdf==0&&draws[3].sdf==0&&draws[4].sdf==fit.edge);
   assert(sdf==0&&swaps[0]==GX_TEV_SWAP0);
   CTextRenderBuffer mid;mid.x4_fonts=b.x4_fonts;mid.AddCharacter({0,0},'0',0xffffffff);
   draws.clear();mid.Render(CColor(0xffffffff),0);assert(draws.empty());
  }
  for(int repeat=0;repeat<16;++repeat){PortHdFont::Reset();assert(!atlas);assert(PortHdFont::Begin(*f));PortHdFont::End();}
  ++layoutCount;puts("layout=unchanged fallback+image+reset=ok");
 }
 CTextRenderBuffer switches;switches.x50_palettes.push_back(std::make_shared<CGraphicsPalette>());
 switches.Palette(0);
 for(size_t i=0;i<fonts.size();++i){switches.x4_fonts.emplace_back(fonts[i].get());switches.Font(i);switches.AddCharacter({int(i)*20,0},'0',0xffffffff);}
 draws.clear();switches.Render(CColor(0xffffffff),0);assert(draws.size()==fonts.size());
 for(size_t i=0;i<fonts.size();++i)assert(draws[i].sdf==(fonts[i]->GetMode()==kFM_OneLayerOutline?38:128));
 assert(sdf==0&&swaps[0]==GX_TEV_SWAP0);
 original=true;assert(!PortHdFont::Begin(*fonts.front()));original=false;
 PortHdFont::SetEnabled(false);assert(!PortHdFont::Begin(*fonts.front()));PortHdFont::SetEnabled(true);
 for(bool supported:{false,true}) {
  webgpu::g_textureComponentSwizzleSupported=supported;wgpu::TextureComponentSwizzleDescriptor sw;
  assert(uses_direct_texture_upload(GX_TF_R8_PC)==supported&&setup_swizzle(sw,GX_TF_R8_PC)==supported);
  assert(to_wgpu(GX_TF_R8_PC)==(supported?wgpu::TextureFormat::R8Unorm:wgpu::TextureFormat::RGBA8Unorm));
  if(supported)assert(sw.swizzle.r==wgpu::ComponentSwizzle::R&&sw.swizzle.a==wgpu::ComponentSwizzle::R);
 }
 assert(SdfShader(0).empty());assert(SdfShader(128).find("fwidth(sdf_d)")!=std::string::npos);
 assert(SdfShader(50).find("0.196078")!=std::string::npos);
 // Loading can precede mod discovery. An empty path remains retryable; a
 // failed file load is latched until the real mod-reload Reset seam runs.
 PortHdFont::Reset();fontPath.clear();assert(!PortHdFont::Begin(*fonts.front()));
 fontPath=validFontPath;assert(PortHdFont::Begin(*fonts.front()));PortHdFont::End();
 PortHdFont::Reset();fontPath=validFontPath+".missing";assert(!PortHdFont::Begin(*fonts.front()));
 fontPath=validFontPath;assert(!PortHdFont::Begin(*fonts.front()));
 PortHdFont::Reset();assert(PortHdFont::Begin(*fonts.front()));PortHdFont::End();
 auto corrupt=std::filesystem::path(argv[2]).parent_path()/"invalid.sdfont";
 {std::ofstream file(corrupt,std::ios::binary);file<<"bad";}
 PortHdFont::Reset();fontPath=corrupt.string();assert(!PortHdFont::Begin(*fonts.front()));
 fontPath=validFontPath;PortHdFont::Reset();assert(PortHdFont::Begin(*fonts.front()));PortHdFont::End();
 std::filesystem::remove(corrupt);
 const CFontInfo saved=*fonts.front()->x30_fontInfo;
 fonts.front()->x30_fontInfo=CFontInfo(false,false,0,14,"AnotherTypeface");
 assert(!PortHdFont::Begin(*fonts.front()));fonts.front()->x30_fontInfo=saved;
 PortHdFont::Reset();assert(loads==destroys&&!atlas&&sdf==0);
 PortHdFont::Reset();assert(loads==destroys);
 printf("HD_FONT actual_render=1 fonts=%d glyphs=%d loads=%d destroys=%d swizzle=capability-gated shader=actual hardware_visual=untested\n",layoutCount,glyphCount,loads,destroys);
}
