// The Python driver inserts the exact production functions at the two markers.
#define PRIME_TRACE_TEST 1
#define TARGET_PC 1
#include <aurora/frame_trace.hpp>
#include <cassert>
#include <cstdio>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define AUTO(name,value) auto name=value

std::vector<std::string> calls, labels;
void note(const char* name) { calls.emplace_back(name); }
void check(std::initializer_list<const char*> expected,
           std::initializer_list<const char*> scopes) {
  std::vector<std::string> want;for(auto value:expected)want.emplace_back(value);
  assert(calls == want);calls.clear();labels.clear();
  for(unsigned i=0;i<aurora::trace::MaxThreads;++i)
    aurora::trace::drain(i,[](const auto& e){
      if(e.kind==aurora::trace::Inclusive) {assert(e.frame==1);labels.emplace_back(e.name);}
    });
#ifdef __ORBIS__
  want.clear();for(auto value:scopes)want.emplace_back(value);assert(labels==want);
#else
  assert(labels.empty());
#endif
}
template<class T> struct Ptr {
  std::shared_ptr<T> ptr;
  Ptr()=default;Ptr(T* p):ptr(p){}
  bool IsNull()const{return !ptr;}T* operator->()const{return ptr.get();}
  T& operator*()const{return *ptr;}
};
namespace rstl {template<class T>using ncrc_ptr=Ptr<T>;template<class T>using auto_ptr=std::unique_ptr<T>;}
#define rs_new new
enum {kAM_TimerTick,kAM_FrameEnd,kAMT_IOWinManager};
struct CArchitectureMessage {int type;int GetType()const{return type;}};
struct CArchitectureQueue {void Push(int){note("push");}};
struct CIOWin {virtual ~CIOWin()=default;enum EMessageReturn{kMR_Exit,kMR_RemoveIOWinAndExit};};
namespace MakeMsg {
struct Tick {float GetReal(){return .016f;}};
Tick GetParmTimerTick(const CArchitectureMessage&){return{};}
int CreateCreateIOWin(int,int,int,CIOWin* game){delete game;return 0;}
}
struct Transition {
  bool finished=false;void StartTransition(){note("start");}
  void Update(float){note("transition");}void WaitForModelsAndTextures(){note("cleanup");}
  void StartTextFadeOut(){note("fade");}bool IsTransitionFinished(){return finished;}
};
struct World {
  int Mailbox(){return 0;}int MapWorldInfo(){return 0;}int GetLayerState(){return 0;}
  int GetWorldAssetId(){return 0;}int GetCurrentArea(){return 0;}int GetDesiredAreaAssetId(){return 0;}
  void SetDesiredAreaAssetId(int){note("clear-desired");}
};
struct GameState {
  Ptr<Transition> transition{new Transition};World world;
  Ptr<Transition>& WorldTransitionManager(){return transition;}
  World& CurrentWorldState(){return world;}int PlayerState(){return 0;}
} gameState;
using CWorldTransManager=Transition;using CWorldState=World;
GameState* gpGameState=&gameState;constexpr int kInvalidAssetId=-1;
bool failCtor=false;
struct CStateManager {
  bool full=false;
  CStateManager(int,int,int,Ptr<Transition>&,int){note("state-create");if(failCtor)throw std::runtime_error("ctor");}
  bool IsFullyInitialized(){return full;}
  void InitializeState(int,int,int){note("world-load");}
};
struct CInGameGuiManager {
  bool ready=false;CInGameGuiManager(CStateManager&,CArchitectureQueue&){note("gui-create");}
  bool CheckLoadComplete(CStateManager&){note("gui-load");return ready;}
};
struct CMFGame:CIOWin {CMFGame(Ptr<CStateManager>,Ptr<CInGameGuiManager>,CArchitectureQueue&){note("game-create");}};
struct CModel {static void EnableTextureTimeout(){note("timeout");}};
struct Token {bool loaded=false;void Lock(){note("lock");}bool IsLoaded(){return loaded;}};
struct CMFGameLoader:CIOWin {
  bool x2c_24_initialized=false,x2c_25_transitionFinished=false;
  std::vector<Token>x1c_loadList;Ptr<CStateManager>x14_stateManager;Ptr<CInGameGuiManager>x18_guiManager;
  void MakeLoadDependancyList(){note("dependencies");}
  EMessageReturn OnMessage(const CArchitectureMessage&,CArchitectureQueue&);
};
/* ACTUAL_LOADER */
struct IObj {};using SObjectTag=int;using CVParamTransfer=int;
IObj obj;int pumps=0,waits=0;bool failPump=false;
struct DVD {bool IsComplete(){return false;}void WaitUntilComplete(){++waits;note("wait");}} dvd;
void ARQPoll(){note("arq");}
struct CResFactory {
  struct Loading {IObj**x10_target;DVD*x8_dvdReq;};
  std::list<Loading>x84_loadList;
  auto FindInLoadList(int){return x84_loadList.begin();}
  bool PumpResource(std::list<Loading>::iterator it,int){
    note("pump");if(failPump)throw std::runtime_error("pump");
    if(++pumps==2){*it->x10_target=&obj;return true;}return false;
  }
  rstl::auto_ptr<IObj> BuildSync(int,int){note("sync");return std::make_unique<IObj>();}
  rstl::auto_ptr<IObj> Build(const SObjectTag&,const CVParamTransfer&);
};
/* ACTUAL_TOKEN */
int main(){
  aurora::trace::begin(aurora::trace::Main,1);
  CArchitectureQueue queue;CMFGameLoader loader;
  assert(loader.OnMessage({kAM_TimerTick},queue)==CIOWin::kMR_Exit);
  check({"dependencies","start"},{});
  loader.x1c_loadList.push_back({false});
  loader.OnMessage({kAM_TimerTick},queue);
  check({"lock","transition"},{"loader transition update (inclusive)"});
  loader.x1c_loadList[0].loaded=true;loader.OnMessage({kAM_TimerTick},queue);
  check({"lock","transition","cleanup","state-create","world-load"},
    {"loader transition update (inclusive)","loader texture and delayed resource cleanup (inclusive)",
     "loader state manager construction (inclusive)","loader world initialization (inclusive)"});
  loader.x14_stateManager->full=true;loader.OnMessage({kAM_TimerTick},queue);
  check({"transition","clear-desired","gui-create","gui-load"},
    {"loader transition update (inclusive)","loader GUI construction (inclusive)","loader GUI dependency preparation (inclusive)"});
  loader.x18_guiManager->ready=true;gameState.transition->finished=true;
  loader.OnMessage({kAM_TimerTick},queue);
  check({"transition","gui-load","fade"},
    {"loader transition update (inclusive)","loader GUI dependency preparation (inclusive)"});
  assert(loader.OnMessage({kAM_FrameEnd},queue)==CIOWin::kMR_RemoveIOWinAndExit);
  check({"game-create","push","timeout"},{});
  loader.x14_stateManager={};failCtor=true;
  try{loader.OnMessage({kAM_TimerTick},queue);assert(false);}catch(const std::runtime_error&){}
  check({"transition","cleanup","state-create"},
    {"loader transition update (inclusive)","loader texture and delayed resource cleanup (inclusive)","loader state manager construction (inclusive)"});
  CResFactory factory;
  factory.Build(1,2);check({"sync"},{"resource token construction (inclusive)"});
  IObj* target=&obj;factory.x84_loadList.push_back({&target,&dvd});
  factory.Build(1,2).release();check({},{"resource token construction (inclusive)"});
  target=nullptr;factory.Build(1,2).release();assert(waits==1&&pumps==2);
  check({"arq","pump","arq","wait","pump"},{"resource token construction (inclusive)"});
  target=nullptr;failPump=true;
  try{factory.Build(1,2);assert(false);}catch(const std::runtime_error&){}
  check({"arq","pump"},{"resource token construction (inclusive)"});
  for(const auto& ring:aurora::trace::rings)assert(ring.dropped.load()==0);
  assert(aurora::trace::unregisteredDrops.load()==0);
  std::puts("loader_paths=7 token_paths=4 order_and_unwind=ok drops=0");
}
