// Differential checks against the complete shader analyser formerly used for
// every prune. Keep the full ShaderConfig comparison: masks also affect keys.
#include "gx/gx.hpp"
#include "gx/shader_info.hpp"
#include "gfx/hash.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#undef assert
#define assert(c) do { if (!(c)) { std::fprintf(stderr,"pruning assertion failed: %s at %d\n",#c,__LINE__); std::abort(); } } while (false)
using namespace aurora;

static void reference_prune(gx::ShaderConfig& config) {
  const auto info = gx::build_shader_info(config);
  for (int i=0;i<info.sampledColorChannels.size();++i) {
    if (info.sampledColorChannels[i] || (i==1 && config.pbr)) continue;
    config.colorChannels[i]={};config.colorChannels[i+GX_ALPHA0]={};
  }
  for (int i=0;i<info.sampledTexCoords.size();++i) {
    if (!info.sampledTexCoords[i]) config.tcgs[i]={};
    config.tcgs[i].mtx=GX_IDENTITY;
  }
  const auto ras=[](GXTevColorArg a){return a==GX_CC_RASC || a==GX_CC_RASA;};
  for (int i=1;i<config.tevStageCount;++i) {
    auto& t=config.tevStages[i];const auto& c=t.colorPass;const auto& a=t.alphaPass;
    if (t.channelId==GX_ALPHA_BUMP || t.channelId==GX_ALPHA_BUMPN ||
        ras(c.a)||ras(c.b)||ras(c.c)||ras(c.d)||
        a.a==GX_CA_RASA||a.b==GX_CA_RASA||a.c==GX_CA_RASA||a.d==GX_CA_RASA) continue;
    t.channelId=GX_COLOR_NULL;
  }
}
static uint32_t randomState=0x5052494d;
static uint32_t random32(){auto& x=randomState;x^=x<<13;x^=x>>17;x^=x<<5;return x;}
static gx::ShaderConfig random_config(unsigned serial) {
  gx::ShaderConfig c{};
  c.tevStageCount=serial%17;c.numIndStages=random32()%5;
  c.lineMode=random32()%4;c.sceneHdr=random32()%2;c.pbr=random32()%2;
  c.fogType=serial%2?GX_FOG_PERSP_LIN:GX_FOG_NONE;c.shadow=random32()%2;
  c.clipPlane=random32()%2;c.shell=random32()%3;c.colorMatrix=random32()%2;
  c.pbrThermal=random32()%2;c.volFog=random32()%4;c.actorDecal=random32()%2;
  c.pbrLightmapAttr=serial%2?GX_VA_TEX0:GX_VA_NULL;
  for (auto& ch:c.colorChannels) {
    ch.lightingEnabled=random32()%2;ch.matSrc=GXColorSrc(random32()%2);
    ch.ambSrc=GXColorSrc(random32()%2);ch.diffFn=GXDiffuseFn(random32()%3);
    ch.attnFn=GXAttnFn(random32()%3);
  }
  constexpr GXChannelID channels[]={GX_COLOR0,GX_COLOR1,GX_ALPHA0,GX_ALPHA1,GX_COLOR0A0,GX_COLOR1A1,GX_COLOR_ZERO,GX_ALPHA_BUMP,GX_ALPHA_BUMPN,GX_COLOR_NULL};
  for (auto& t:c.tevStages) {
    t.colorPass={GXTevColorArg(random32()%16),GXTevColorArg(random32()%16),GXTevColorArg(random32()%16),GXTevColorArg(random32()%16)};
    t.alphaPass={GXTevAlphaArg(random32()%8),GXTevAlphaArg(random32()%8),GXTevAlphaArg(random32()%8),GXTevAlphaArg(random32()%8)};
    t.colorOp.outReg=GXTevRegID(random32()%4);t.alphaOp.outReg=GXTevRegID(random32()%4);
    t.channelId=channels[random32()%std::size(channels)];
    t.texCoordId=GXTexCoordID(random32()%8);t.texMapId=GXTexMapID(random32()%8);
    if (random32()%7==0) t.texMapId=GX_TEXMAP_NULL;
    if (t.texMapId==GX_TEXMAP_NULL && random32()%2) t.texCoordId=GX_TEXCOORD_NULL;
    t.indTexStage=GXIndTexStageID(random32()%4);
    constexpr GXIndTexMtxID matrices[]={GX_ITM_OFF,GX_ITM_0,GX_ITM_1,GX_ITM_2,GX_ITM_S0,GX_ITM_S1,GX_ITM_S2,GX_ITM_T0,GX_ITM_T1,GX_ITM_T2};
    t.indTexMtxId=matrices[random32()%std::size(matrices)];
    t.indTexWrapS=GXIndTexWrap(random32()%7);t.indTexWrapT=GXIndTexWrap(random32()%7);
    t.indTexAddPrev=random32()%2;
  }
  for(auto& t:c.indStages){t.texCoordId=GXTexCoordID(random32()%8);t.texMapId=GXTexMapID(random32()%8);}
  for(auto& t:c.tcgs){
    t.type=GXTexGenType(random32()%11);t.src=GXTexGenSrc(random32()%21);
    t.mtx=GXTexMtx((random32()%10)*3+GX_TEXMTX0);
    t.postMtx=GXPTTexMtx((random32()%20)*3+GX_PTTEXMTX0);t.embossSrc=random32()%8;
    t.normalize=random32()%2;
  }
  return c;
}
static void compare(const gx::ShaderConfig& input,unsigned serial) {
  auto candidate=input,expected=input;
  gx::pipeline_unused_inputs(candidate);reference_prune(expected);
  if (std::memcmp(&candidate,&expected,sizeof(candidate))) {
    std::fprintf(stderr,"pruning bytes differ: case=%u seed=%08x\n",serial,randomState);std::abort();
  }
  assert(xxh3_hash(candidate)==xxh3_hash(expected));
}
static uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(){
  constexpr unsigned cases=131072;
  for(unsigned i=0;i<cases;++i)compare(random_config(i),i);
  // A forward emboss source may add a later source; a source already passed
  // is not recursively reprocessed. Exercise both orders and colour texgens.
  for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b)for(unsigned src=0;src<8;++src){
    auto c=random_config(1);c.numIndStages=0;
    c.tevStages[0].colorPass={GX_CC_TEXC,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO};
    c.tevStages[0].texCoordId=GXTexCoordID(a);c.tevStages[0].texMapId=GX_TEXMAP0;
    c.tcgs[a].type=GX_TG_BUMP0;c.tcgs[a].embossSrc=b;
    c.tcgs[b].type=GX_TG_BUMP1;c.tcgs[b].embossSrc=src;
    c.tcgs[src].type=GX_TG_MTX2x4;c.tcgs[src].src=GX_TG_COLOR1;
    compare(c,cases+a*64+b*8+src);
  }
  std::printf("PRUNE_EQUIVALENCE configs=%u full_config_bytes=%zu hash_equal=1 seed=5052494d\n",cases+512,sizeof(gx::ShaderConfig));
  std::vector<gx::ShaderConfig> inputs;
  for(unsigned i=0;i<256;++i)inputs.push_back(random_config(i));
  uint64_t checksum=0;
  for(unsigned pass=0;pass<2;++pass){
    std::array<uint64_t,9> elapsed{};
    for(unsigned round=0;round<11;++round){
      const auto start=now();
      for(unsigned j=0;j<16384;++j){
        auto c=inputs[j%inputs.size()];
        if(pass)gx::pipeline_unused_inputs(c);else reference_prune(c);
        checksum+=c.tcgs[j%8].src+c.colorChannels[j%4].lightingEnabled+c.tevStages[j%16].channelId;
      }
      if(round>=2)elapsed[round-2]=now()-start;
    }
    std::sort(elapsed.begin(),elapsed.end());
    std::printf("PRUNE_BENCH path=%s configs=16384 median_ms=%.6f min_ms=%.6f max_ms=%.6f checksum=%llu\n",pass?"candidate":"reference",elapsed[4]/1e6,elapsed.front()/1e6,elapsed.back()/1e6,(unsigned long long)checksum);
  }
}
