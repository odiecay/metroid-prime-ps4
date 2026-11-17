// Controlled providers execute the exact production initialization/streaming
// bodies. No disc timings or performance improvements are simulated here.
#define PRIME_TRACE_TEST 1
#define TARGET_PC 1
#include <aurora/frame_trace.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <set>
#include <thread>
#include <string>
#include <vector>
using uint=unsigned;using CAssetId=unsigned;using TEditorId=unsigned;
#define AUTO(a,b) auto a=b
#define rs_new new
std::set<std::string> seenLabels;
std::vector<std::string> calls;std::string throwAt;unsigned exceptions=0;
void note(const char* s){calls.emplace_back(s);if(throwAt==s)throw std::runtime_error(s);}
struct TAreaId{int value=0;TAreaId(int v=0):value(v){}int Value()const{return value;}bool operator==(const TAreaId&)const=default;};
constexpr CAssetId kInvalidAssetId=~0u;const TAreaId kInvalidAreaId(-1);constexpr unsigned kInvalidUniqueId=~0u,kSM_WorldInitialized=1;
struct TUniqueId{unsigned value=0;TUniqueId(unsigned v=0):value(v){}unsigned Value()const{return value;}bool operator!=(unsigned v)const{return value!=v;}};
struct CVector3f{void SetZ(float){}bool CanBeNormalized()const{return true;}static CVector3f Up(){return{};}CVector3f operator+(CVector3f)const{return{};}};
struct CTransform4f{CVector3f GetTranslation()const{return{};}CVector3f GetForward()const{return{};}static CTransform4f LookAt(CVector3f,CVector3f,CVector3f){return{};}};
struct CEntity{virtual~CEntity()=default;};
struct CScriptSpawnPoint:CEntity{bool active=true,first=true;bool GetActive()const{return active;}bool FirstSpawn()const{return first;}CTransform4f GetTransform()const{return{};}int GetPowerup(int)const{return 2;}};
template<class T>T* TCastToPtr(CEntity* p){return dynamic_cast<T*>(p);}template<class T>const T* TCastToConstPtr(CEntity* p){return dynamic_cast<T*>(p);}
struct CObjectList{std::vector<CEntity*>objects;int GetFirstObjectIndex(){return objects.empty()?-1:0;}int GetNextObjectIndex(int i){return ++i<int(objects.size())?i:-1;}CEntity*operator[](int i){return objects[i];}};
struct CPlayerState{enum EItemType{kIT_PowerBeam,kIT_Other,kIT_Max};int GetPowerUp(int){return 0;}int GetItemAmount(int){return 0;}void InitializePowerUp(int,int){note("powerup");}void IncrPickUp(int,int){note("pickup");}};
struct CStateManager;struct CGameArea;struct CWorld;
struct Player{void Teleport(CTransform4f,CStateManager&,bool){note("teleport");}void AsyncLoadSuit(CStateManager&){note("suit");}};
struct Camera{void ResetCameras(CStateManager&){note("camera");}};struct EnvFx{void AsyncLoadResources(CStateManager&){note("env");}};struct CRandom16{};
struct SObjectTag{unsigned type,id;SObjectTag(unsigned t,unsigned i):type(t),id(i){}};
struct Pool{SObjectTag GetObj(SObjectTag t){note("pool");return t;}} pool;Pool*gpSimplePool=&pool;
struct ResourceLoader{void FindResource(SObjectTag){note("find-resource");}};
struct Factory{ResourceLoader loader;ResourceLoader&GetResLoader(){return loader;}}factory;Factory*gpResourceFactory=&factory;
struct WorldState{void SetAreaId(TAreaId){note("set-world-area");}};
struct GameState{WorldState world;bool init=true;WorldState&CurrentWorldState(){return world;}bool GetInitPowerupsAtFirstSpawn(){return init;}void SetDeferPowerupInit(bool){note("defer-powerup");}}gameState;GameState*gpGameState=&gameState;
struct CStreamAudioManager{static void SetDefaultAudio(const std::string&,float,float,char){note("default-audio");}static void StopAll(){note("music-stop");}};bool sPortWarpMusicPending=false;bool PortStartAreaMusic(CStateManager&){note("music");return false;}
struct Cache{bool ready=true;bool TryCache(){note("shadow-cache");return ready;}};
struct CMemoryInStream{static inline std::deque<int>values;CMemoryInStream(void*,unsigned){note("stream-open");}int ReadLong(){note("stream-long");assert(!values.empty());int v=values.front();values.pop_front();return v;}template<class T>T Get(){return T(ReadLong());}};
namespace rstl{struct string:std::string{using std::string::string;string(const std::string&s):std::string(s){}string(CMemoryInStream&):std::string("track"){note("stream-string");}};template<class T>struct vector:std::vector<T>{using std::vector<T>::vector;vector()=default;vector(CMemoryInStream&){note("stream-vector");}};}
struct CModel{bool loaded=true;void Touch(int){note("sky-touch");}bool IsLoaded(int){note("sky-loaded");return loaded;}};
struct CMapWorld{bool streaming=false;void SetWhichMapAreasLoaded(CWorld&,int,int){note("map-areas");}bool IsMapAreasStreaming(){note("map-streaming");return streaming;}};
template<class T>struct TCachedToken{T object;bool ready=true;TCachedToken()=default;TCachedToken(SObjectTag){note("cache-create");}void Lock(){note("cache-lock");}bool TryCache(){note("cache-try");return ready;}T*GetObject(){note("cache-get");return &object;}};
template<class T>struct TLockedToken{TLockedToken(TCachedToken<T>&){note("cache-own");}};
struct AudioData{bool loaded=true;void Lock(){note("audio-lock");}bool IsLoaded(){note("audio-loaded");return loaded;}};
struct CAudioSys{static void GetVerbose(){note("audio-verbose");}};
struct AudioTweak{std::string GetFileName(){return"track";}float GetVolume(){return 1.f;}};
struct TweakValue{AudioTweak GetAudio(){return{};}};
struct TweakManager{bool HasTweakValue(const std::string&){return false;}TweakValue*GetTweakValue(const std::string&){return nullptr;}}tweak;TweakManager*gpTweakManager=&tweak;
struct CInGameTweakManager{static std::string GetIdentifierForWorldDefaultMusic(unsigned){return"world";}};
struct CCast{static char ToInt8(float x){return char(x);}};
struct CScriptStreamedMusic{static bool IsAudioTrackNameSoftware(const std::string&){return true;}};
struct CWorldLayers{static void ReadWorldLayers(CMemoryInStream&,int,unsigned){note("world-layers");}};
struct CMemory{static void Free(void*){note("free-world-buffer");}};
struct Buf{char data[32]{};char*get(){return data;}char*release(){return data;}};
struct Request{bool complete=true;bool IsComplete(){note("request-ready");return complete;}void WaitUntilComplete(){note("request-wait");complete=true;}};
struct CRelay{};
struct CTexture{void MakeSwappable(){note("swappable");}void LoadToARAM(){note("load-aram");}};
struct CToken{bool loaded=true;unsigned type='TXTR';CTexture texture;bool IsLoaded(){note("token-loaded");return loaded;}void Lock(){note("token-lock");}unsigned GetReferenceType(){return type;}void*GetObj(){note("token-get");return this;}};
template<class T>struct TToken{CToken&token;TToken(CToken&t):token(t){}T*GetT(){note("texture-get");return &token.texture;}};
namespace PortRoomGeo{void Prefetch(unsigned){note("geometry-prefetch");}}
void aurora_pipeline_prefetch(unsigned){note("pipeline-prefetch");}unsigned PortPipelineAreaContext(unsigned a,int b){return a^b;}
#define AURORA_PIPELINE_CONTEXT_WORLD(a) ((a)^0xffffu)
bool PortAreaLogEnabled(){return false;}std::map<int,std::chrono::steady_clock::time_point>sPortAreaLoadStart;long long PortAreaLoadMs(int){return 0;}
struct CPVSAreaSet{int GetNumActors()const{return 1;}unsigned GetEntityIdByIndex(int)const{return 3;}int GetNumFeatures()const{return 2;}};
struct SPVSActorInfo{SPVSActorInfo()=default;SPVSActorInfo(int,TUniqueId){note("pvs-actor");}};
struct CGameArea{
 enum{kP_LoadHeader,kP_WaitForFinish};bool xf0_24_postConstructed=false,xf0_27_loadPaused=false,xf0_26_tokensReady=false,xf0_28_validated=false;
 TAreaId x4_selfIdx{0};unsigned x84_mrea=1;int xf4_phase=kP_WaitForFinish;unsigned x120_unk=1024;
 std::vector<CToken>xdc_tokens;std::deque<std::shared_ptr<Request>>xf8_loadTransactions;
 struct CPostConstructed{std::shared_ptr<CPVSAreaSet>xa0_pvs;bool x1108_29_pvsHasActors=false;std::array<SPVSActorInfo,8>xa4_pvsEntityMap;};
 std::unique_ptr<CPostConstructed>x12c_postConstructed{new CPostConstructed};
 unsigned streamSteps=0;bool streamPending=false;bool verifyAddsToken=false;
 CGameArea()=default;CGameArea(CMemoryInStream&,int i,int):x4_selfIdx(i) {note("area-create");}
 unsigned GetAreaAssetId(){return x84_mrea;}TAreaId GetId(){return x4_selfIdx;}
 void VerifyTokenList(CStateManager&){note("verify-tokens");if(verifyAddsToken&&xdc_tokens.empty())xdc_tokens.emplace_back();}
 bool StartStreamingMainArea(){note("stream-sections");if(streamSteps){--streamSteps;return true;}return false;}
 void CullDeadAreaRequests(){note("retire-requests");while(!xf8_loadTransactions.empty()&&xf8_loadTransactions.front()->complete)xf8_loadTransactions.pop_front();}
 void PostConstructArea(){note("postconstruct");xf0_24_postConstructed=true;}
 void LoadScriptObjects(CStateManager&){note("area-scripts");}
 void Validate(CStateManager&);void StartStreamIn(CStateManager&);
};
template<class T>struct Owner:std::unique_ptr<T>{using std::unique_ptr<T>::unique_ptr;Owner()=default;Owner(T*p):std::unique_ptr<T>(p){}Owner(Owner&&)=default;Owner&operator=(Owner&&)=default;Owner&operator=(T*p){this->reset(p);return*this;}};
struct CWorld{
 enum{kP_Loading,kP_LoadingMap,kP_LoadingMapAreas,kP_LoadingSkyBox,kP_LoadingSoundGroups,kP_Done,kC_Deallocated,kATT_SkipAdjacent};
 struct CSoundGroupData{int x0_groupId=0;unsigned x4_agscId=0;bool x8_25_loaded=false;std::shared_ptr<AudioData>x1c_groupData;CSoundGroupData(int a,unsigned b):x0_groupId(a),x4_agscId(b),x1c_groupData(new AudioData){note("audio-create");}};
 int x4_phase=kP_Done;unsigned x8_mlvlId=1,xc_strgId=0,x10_savwId=0,x24_mapwId=0,x44_bufSz=32;
 std::vector<Owner<CGameArea>>x18_areas;Owner<TCachedToken<CMapWorld>>x28_mapWorld{new TCachedToken<CMapWorld>};rstl::vector<CRelay>x2c_relays;
 std::shared_ptr<Request>x3c_loadToken{new Request};Buf x40_loadBuf;std::vector<CGameArea*>x48_chainHeads;TAreaId x68_curAreaId{0};
 std::vector<CSoundGroupData>x74_soundGroupData;std::string x84_defAudioTrack;std::optional<TCachedToken<CModel>>x94_skyboxWorld;std::optional<TLockedToken<CModel>>xa4_skyboxWorldLoaded;bool x70_26_skyboxActive=false,x70_27_skyboxVisible=false;
 bool schedule=false;CWorld(){x18_areas.emplace_back(new CGameArea);}CWorld(Pool&,Factory&,unsigned):CWorld(){note("world-create");}
 CGameArea*GetArea(TAreaId i){return x18_areas.at(i.Value()).get();}CGameArea*Area(TAreaId i){return GetArea(i);}TAreaId GetCurrentAreaId(){return x68_curAreaId;}unsigned IGetWorldAssetId()const{return x8_mlvlId;}
 bool ScheduleAreaToLoad(CGameArea*,CStateManager&){note("schedule-area");return schedule;}
 CMapWorld*GetMapWorld(){return &x28_mapWorld->object;}void MoveToChain(CGameArea*,int){note("area-chain");}void MoveAreaToChain3(TAreaId){note("area-alive");}
 void LoadSoundGroup(int,unsigned,CSoundGroupData&d){note("audio-load");d.x8_25_loaded=true;}void LoadSoundGroups(){note("audio-groups");}
 void TravelToArea(TAreaId,CStateManager&,int);bool CheckWorldComplete(CStateManager*,TAreaId,unsigned);
};
struct CStateManager{
 enum{kIP_LoadWorld,kIP_LoadFirstArea,kIP_Done};enum{kOL_All=0};int xb3c_initPhase=kIP_LoadWorld;CRandom16*x900_random=nullptr,x8fc_random;CWorld*x850_world=nullptr;Cache x8f0_shadowTex;TAreaId x8cc_nextAreaId{0};
 Player player;Camera camera;EnvFx env;Player*x84c_player=&player;Camera*x870_cameraManager=&camera;EnvFx*x880_envFxManager=&env;
 std::array<std::unique_ptr<CObjectList>,1>x808_objectLists;CPlayerState state;
 CStateManager(){x808_objectLists[0]=std::make_unique<CObjectList>();}~CStateManager(){delete x850_world;}
 void CreateStandardGameObjects(){note("standard-objects");}bool IsFullyInitialized()const{return xb3c_initPhase==kIP_Done;}
 TAreaId GetNextAreaId(){return x8cc_nextAreaId;}void SetCurrentAreaId(TAreaId){note("set-current-area");}void UpdateRoomAcoustics(TAreaId){note("acoustics");}
 void DeliverScriptMsg(CEntity*,unsigned,unsigned){note("world-script");}CPlayerState*GetPlayerState(){return &state;}
 CWorld*GetWorld(){return x850_world;}CWorld*World(){return x850_world;}void LoadScriptObjects(TAreaId,CMemoryInStream&,rstl::vector<TEditorId>&){note("world-scripts");}void InitScriptObjects(rstl::vector<TEditorId>&){note("init-scripts");}
 TUniqueId GetIdForScript(unsigned){return 0;}void AreaLoaded(TAreaId){note("area-loaded");}
 void InitializeState(unsigned,TAreaId,unsigned);
};
void CWorld::TravelToArea(TAreaId a,CStateManager&m,int){note("travel");Area(a)->Validate(m);}
// Extracted original functions, never reimplement their control flow here.
/* ACTUAL_GUARD */
/* ACTUAL_INITIALIZE */
/* ACTUAL_WORLD */
/* ACTUAL_VALIDATE */
/* ACTUAL_STREAM */

std::vector<std::string> drain(bool want){
 std::vector<std::string>labels;
 for(unsigned i=0;i<aurora::trace::MaxThreads;++i)aurora::trace::drain(i,[&](const auto&e){assert(e.kind==aurora::trace::Inclusive);assert(e.frame==73);labels.emplace_back(e.name);});
#if defined(__ORBIS__) && defined(PRIME_LOADER_DEEP_TRACE_EXPECTED)
 if(want)assert(!labels.empty());else assert(labels.empty());
#else
 assert(labels.empty());
#endif
 for(const auto&label:labels)seenLabels.insert(label);
 return labels;
}
void printCase(unsigned id,const CStateManager& m){
 std::printf("%u:",id);for(auto&s:calls)std::printf("%s,",s.c_str());
 const CWorld*w=m.x850_world;const CGameArea*area=w&&!w->x18_areas.empty()?w->x18_areas[0].get():nullptr;
 std::printf(" phase=%d random=%d worldphase=%d area=%d tokens=%zu ready=%d validated=%d requests=%zu music=%d\n",
  m.xb3c_initPhase,m.x900_random!=nullptr,w?w->x4_phase:-1,area?area->xf0_24_postConstructed:0,
  area?area->xdc_tokens.size():0,area?area->xf0_26_tokensReady:0,area?area->xf0_28_validated:0,
  area?area->xf8_loadTransactions.size():0,sPortWarpMusicPending);
 calls.clear();
}
int main(){
 aurora::trace::begin(aurora::trace::Main,73);
 unsigned id=0;
 // First/remaining initialization, early returns, authored spawn/inventory and
 // direct TravelToArea->Validate after phase becomesDone must all stay ordered.
 for(unsigned mode=0;mode<6;++mode){
  CStateManager m;CScriptSpawnPoint spawn;m.x808_objectLists[0]->objects.push_back(&spawn);
  m.x8f0_shadowTex.ready=mode!=0;sPortWarpMusicPending=mode==5;
  if(mode){m.x850_world=new CWorld;m.xb3c_initPhase=CStateManager::kIP_LoadFirstArea;}
  if(mode==1){m.x850_world->x4_phase=CWorld::kP_Loading;m.x850_world->x3c_loadToken->complete=false;}
  if(mode==2||mode==3){m.x850_world->schedule=true;auto*a=m.x850_world->Area(0);a->xdc_tokens.push_back({mode==3});}
  if(mode>=4){auto*a=m.x850_world->Area(0);a->streamSteps=2;a->xf8_loadTransactions.push_back(std::make_shared<Request>());a->verifyAddsToken=true;}
  m.InitializeState(1,0,kInvalidAssetId);drain(true);printCase(id++,m);
#ifdef __ORBIS__
  assert(!PortLoaderInitTraceActive());
#endif
 }
 // Fail each real external operation observed in a successful full init; its
 // scope and loader guard must retire even when the production body unwinds.
 const std::vector<std::string>failurePoints={"standard-objects","world-create","shadow-cache","schedule-area","travel","stream-sections","request-wait","verify-tokens","token-lock","token-get","postconstruct","area-scripts","world-script","teleport","suit","camera","env","music"};
 for(const auto&point:failurePoints){
  CStateManager m;CScriptSpawnPoint spawn;m.x808_objectLists[0]->objects.push_back(&spawn);sPortWarpMusicPending=true;
  if(point!="standard-objects"&&point!="world-create"){m.x850_world=new CWorld;m.xb3c_initPhase=CStateManager::kIP_LoadFirstArea;auto*a=m.x850_world->Area(0);a->verifyAddsToken=true;a->xf8_loadTransactions.push_back(std::make_shared<Request>());}
  throwAt=point;bool threw=false;try{m.InitializeState(1,0,kInvalidAssetId);}catch(const std::runtime_error&){threw=true;++exceptions;}throwAt.clear();assert(threw);drain(true);printCase(id++,m);
#ifdef __ORBIS__
  assert(!PortLoaderInitTraceActive());
#endif
 }
 // Normal streaming and dummy-world/map readiness must not emit loader clocks
 // or events. Validate still waits and constructs the exact same resources.
 for(unsigned mode=0;mode<5;++mode){CStateManager m;m.xb3c_initPhase=CStateManager::kIP_Done;m.x850_world=new CWorld;auto*a=m.x850_world->Area(0);a->verifyAddsToken=true;
  if(mode==0)a->StartStreamIn(m);if(mode==1)a->Validate(m);if(mode==2){m.x850_world->x4_phase=CWorld::kP_LoadingMap;m.x850_world->CheckWorldComplete(nullptr,0,kInvalidAssetId);}if(mode==3)a->xf0_27_loadPaused=true;if(mode==4)a->xf0_24_postConstructed=true;
  if(mode>=3)a->StartStreamIn(m);drain(false);printCase(id++,m);}
 // Remaining world phase fallthrough/cache/sky/audio paths under the exact
 // active gate, including early-return failure and exceptions from each phase.
 for(unsigned mode=0;mode<7;++mode){CStateManager m;m.x850_world=new CWorld;auto&w=*m.x850_world;w.x4_phase=CWorld::kP_LoadingMap;w.x94_skyboxWorld.emplace();w.x74_soundGroupData.emplace_back(1,2);
  if(mode==0)w.x28_mapWorld->ready=false;if(mode==1)w.x28_mapWorld->object.streaming=true;if(mode==2)w.x94_skyboxWorld->ready=false;if(mode==3)w.x94_skyboxWorld->object.loaded=false;if(mode==4)w.x74_soundGroupData[0].x1c_groupData->loaded=false;
#ifdef __ORBIS__
  {PortLoaderInitTraceGuard gate;w.CheckWorldComplete(&m,0,kInvalidAssetId);}
#else
  w.CheckWorldComplete(&m,0,kInvalidAssetId);
#endif
  drain(true);printCase(id++,m);}
 // Full MLVL parse falls through map, sky and sound phases without changing
 // which dependency is touched or when script initialization occurs.
 {CStateManager m;m.x850_world=new CWorld;auto&w=*m.x850_world;
  w.x4_phase=CWorld::kP_Loading;w.x18_areas.clear();
  CMemoryInStream::values={0,17,1,2,3,2,0,4,0};
#ifdef __ORBIS__
  {PortLoaderInitTraceGuard gate;assert(w.CheckWorldComplete(&m,0,kInvalidAssetId));}
#else
  assert(w.CheckWorldComplete(&m,0,kInvalidAssetId));
#endif
  assert(CMemoryInStream::values.empty());drain(true);printCase(id++,m);}
 for(const auto&point:std::vector<std::string>{"cache-try","map-areas","map-streaming","sky-touch","sky-loaded","cache-own","audio-lock","audio-loaded","audio-load","audio-groups"}){
  CStateManager m;m.x850_world=new CWorld;auto&w=*m.x850_world;w.x4_phase=CWorld::kP_LoadingMap;w.x94_skyboxWorld.emplace();w.x74_soundGroupData.emplace_back(1,2);throwAt=point;bool threw=false;
  try{
#ifdef __ORBIS__
   PortLoaderInitTraceGuard gate;
#endif
   w.CheckWorldComplete(&m,0,kInvalidAssetId);
  }catch(const std::runtime_error&){threw=true;++exceptions;}
  throwAt.clear();assert(threw);drain(true);printCase(id++,m);
#ifdef __ORBIS__
  assert(!PortLoaderInitTraceActive());
#endif
 }
#ifdef __ORBIS__
 {assert(!PortLoaderInitTraceActive());PortLoaderInitTraceGuard outer;assert(PortLoaderInitTraceActive());
  {PortLoaderInitTraceGuard nested;assert(PortLoaderInitTraceActive());}
  assert(PortLoaderInitTraceActive());bool other=true;std::thread t([&]{other=PortLoaderInitTraceActive();});t.join();assert(!other);}
 assert(!PortLoaderInitTraceActive());
#endif
#if defined(__ORBIS__) && defined(PRIME_LOADER_DEEP_TRACE_EXPECTED)
 static const char* expectedLabels[]={
#include "actual_label_names.inc"
 };
 for(const char*label:expectedLabels)assert(seenLabels.count(label));
 assert(seenLabels.size()==std::size(expectedLabels));
#endif
 for(const auto&r:aurora::trace::rings)assert(r.dropped.load()==0);assert(aurora::trace::unregisteredDrops.load()==0);
 std::fprintf(stderr,"deep_loader_cases=%u injected_exceptions=%u drops=0\n",id,exceptions);
}
