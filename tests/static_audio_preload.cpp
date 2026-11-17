// Production player, rstl owners and DVD worker. Only heap/audio/ARAM providers
// are controlled; overlay gates hold real read and callback ownership.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <Kyoto/Audio/CAudioSys.hpp>
#include <Kyoto/CDvdFile.hpp>
#include <Kyoto/CARAMManager.hpp>
#include <aurora/dvd.h>
#include <dolphin/ai.h>
#define private public
#include <Kyoto/Audio/CStaticAudioPlayer.hpp>
#undef private

static thread_local int failNew=-1;
void* operator new(size_t n) {
  if (failNew>=0 && failNew--==0) { failNew=-1; throw std::bad_alloc(); }
  if (void* p=std::malloc(n?n:1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
void operator delete[](void* p,size_t) noexcept { std::free(p); }

static std::mutex heapMutex;
static std::array<void*,64> allocations{};
static std::atomic<unsigned> liveAllocations=0, frees=0;
static thread_local void (*beforeGameFailure)()=nullptr;
static thread_local int failGame=-1;
void* CMemory::Alloc(size_t n,IAllocator::EHint,IAllocator::EScope,IAllocator::EType,const CCallStack&) {
  if (failGame>=0 && failGame--==0) { failGame=-1; if(beforeGameFailure) beforeGameFailure(); throw std::bad_alloc(); }
  void* p=nullptr; if (posix_memalign(&p,32,n)) throw std::bad_alloc();
  std::memset(p,0xcd,n);
  std::lock_guard lock(heapMutex);
  for(auto& slot:allocations) if(!slot) { slot=p; ++liveAllocations; return p; }
  std::abort();
}
static std::atomic<void*> writer{},callbackBuffer{};
void CMemory::Free(const void* p) {
  if (!p) return;
  assert(p!=writer.load() && p!=callbackBuffer.load());
  std::lock_guard lock(heapMutex);
  for(auto& slot:allocations) if(slot==p) { slot=nullptr; --liveAllocations; ++frees; std::free(const_cast<void*>(p)); return; }
  std::abort();
}
static bool aiEnabled=false;
static AIDCallback dmaCallback=nullptr;
static uintptr_t dmaAddress=0;
alignas(32) static ushort oldDma[320]{};
const ushort CAudioSys::kVolumeTable[128]={0,32768};
bool CAudioSys::IsAICallbackEnabled() { return aiEnabled; }
void CAudioSys::EnableAICallback(bool b) { aiEnabled=b; }
extern "C" AIDCallback AIRegisterDMACallback(AIDCallback fn) { auto old=dmaCallback;dmaCallback=fn;return old; }
extern "C" void AIInitDMA(uintptr_t addr,u32 len) { assert(len==640);dmaAddress=addr; }
extern "C" uintptr_t AIPortGetDMAStartAddr() { return reinterpret_cast<uintptr_t>(oldDma); }
extern "C" BOOL OSDisableInterrupts() { return TRUE; }
extern "C" BOOL OSEnableInterrupts() { return TRUE; }
extern "C" BOOL OSRestoreInterrupts(BOOL b) { return b; }
extern "C" void OSYieldThread() { std::this_thread::yield(); }
extern "C" void DCFlushRange(void*,u32) {}

struct Overlay {
  static constexpr unsigned Length=0x40008, Chunk=0x20000;
  std::mutex mutex;std::condition_variable cv;
  unsigned holdReads=0,entered=0,released=0;
  bool holdCallback=false,callbackEntered=false,callbackReleased=false;
  std::atomic<unsigned> opens=0,successfulOpens=0,closes=0;
  unsigned failOpen=0;int badOffset=-1,badResult=0;
  struct Handle { Overlay* owner;unsigned offset=0; };
  static Overlay* current;
  Overlay(unsigned held=0):holdReads(held) {
    current=this;
    AuroraOverlayCallbacks callbacks{
      .open=[](void* p)->void* {
        auto* self=static_cast<Overlay*>(p);unsigned n=++self->opens;
        if(self->failOpen==n) {self->waitRead(0);return nullptr;}
        auto* result=new Handle{self};++self->successfulOpens;return result;
      },
      .close=[](void* p) { auto* h=static_cast<Handle*>(p);++h->owner->closes;delete h; },
      .read=[](void* p,uint8_t* dst,size_t n)->int64_t {
        auto& h=*static_cast<Handle*>(p);auto& s=*h.owner;
        const unsigned id=h.offset/Chunk;
        writer.store(dst);
        {std::unique_lock lock(s.mutex);s.entered|=1u<<id;s.cv.notify_all();
          if(s.holdReads&(1u<<id)) assert(s.cv.wait_for(lock,std::chrono::seconds(5),[&]{return (s.released&(1u<<id))!=0;}));}
        int64_t result=std::min<size_t>(n,Length-h.offset);
        if(s.badOffset>=0 && h.offset>=unsigned(s.badOffset)) {
          if(s.badResult<0) result=s.badResult;
          else result=std::min<int64_t>(result,std::max<int64_t>(0,int64_t(s.badOffset)+s.badResult-h.offset));
        }
        if(result>0) for(int64_t i=0;i<result;++i) dst[i]=byte(h.offset+unsigned(i));
        h.offset+=result>0?unsigned(result):0;
        writer.store(nullptr);return result;
      },
      .seek=[](void* p,int64_t offset,int32_t whence)->int64_t {
        auto& h=*static_cast<Handle*>(p);if(whence==2) return h.owner->badOffset>=0 && h.owner->badResult>=0 ? h.owner->badOffset+h.owner->badResult : Length;assert(whence==0);h.offset=unsigned(offset);return offset;
      }
    };
    aurora_dvd_overlay_callbacks(&callbacks);
    AuroraOverlayFile file{"/__static_audio__/test.rsf",this,Length};
    aurora_dvd_overlay_files(&file,1,nullptr);
  }
  ~Overlay() {
    {std::lock_guard lock(mutex);released=~0u;callbackReleased=true;cv.notify_all();}
    aurora_dvd_overlay_files(nullptr,0,nullptr);current=nullptr;
    // A failed open contributes to opens but owns no handle.
    assert(successfulOpens==closes);
    assert(liveAllocations==0 && !writer && !callbackBuffer && !dmaCallback);
  }
  static uchar byte(unsigned offset) { return uchar(offset*17u+11u); }
  void waitRead(unsigned id) {
    std::unique_lock lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(5),[&]{return (entered&(1u<<id))!=0;}));
  }
  void release(unsigned id) { std::lock_guard lock(mutex);released|=1u<<id;cv.notify_all(); }
  void waitCallback() {std::unique_lock lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(5),[&]{return callbackEntered;}));}
  void releaseCallback() {std::lock_guard lock(mutex);callbackReleased=true;cv.notify_all();}
};
Overlay* Overlay::current=nullptr;
extern "C" void DCInvalidateRange(void* p,u32) {
  auto* s=Overlay::current;if(!s || !s->holdCallback) return;
  callbackBuffer.store(p);
  {std::unique_lock lock(s->mutex);s->callbackEntered=true;s->cv.notify_all();
    assert(s->cv.wait_for(lock,std::chrono::seconds(5),[&]{return s->callbackReleased;}));}
  callbackBuffer.store(nullptr);
}
// Static RSFs use ordinary DVD files. ARAM providers fail loudly if reached.
const int CARAMManager::kInvalidAlloc=-1,CARAMManager::kInvalidHandle=-1;
void* CARAMManager::Alloc(uint) {std::abort();}
bool CARAMManager::Free(const void*) {std::abort();}
int CARAMManager::DMAToMRAM(void*,void*,uint,EDMAPriority) {std::abort();}
bool CARAMManager::IsDMACompleted(uint) {std::abort();}
void CARAMManager::WaitForDMACompletion(uint) {std::abort();}
extern "C" void ARQPoll() {std::abort();}
extern "C" void ARQPostRequest(ARQRequest*,u32,u32,u32,uintptr_t,uintptr_t,u32,ARQCallback) {std::abort();}

#include STATIC_AUDIO_SOURCE

static constexpr int LoopStart=0x3ffd8,LoopEnd=Overlay::Length;
static CStaticAudioPlayer* makePlayer() {return new CStaticAudioPlayer(rstl::string("/__static_audio__/test.rsf"),LoopStart,LoopEnd);}
static void ready(CStaticAudioPlayer& p) { for(int i=0;!p.IsReady();++i) {assert(i<1000000);std::this_thread::yield();} }
static void checkBytes(CStaticAudioPlayer& p) {
  for(unsigned i=0;i<Overlay::Length;++i) assert(p.x48_buffers[i/Overlay::Chunk].get()[i%Overlay::Chunk]==Overlay::byte(i));
}
static ushort referenceSample(int decoded) {
  // Preserve the game's signed 32-bit volume multiplication (-fwrapv),
  // arithmetic shift and clamp, even for adversarial ADPCM source bytes.
  int scaled=int32_t(uint32_t(decoded)*32768u)>>15;
  return ushort(std::clamp(scaled,-32768,32767));
}
static void checkMix(CStaticAudioPlayer& p) {
  g72x_state left,right;g72x_init_state(&left);g72x_init_state(&right);
  for(unsigned pass=0;pass<3;++pass) {
    if(pass==1) {p.x18_curSamp=LoopEnd-80;g72x_init_state(&p.x58_leftState);g72x_init_state(&p.x8c_rightState);g72x_init_state(&left);g72x_init_state(&right);}
    int sample=p.x18_curSamp;
    std::array<ushort,320> expected;
    for(unsigned i=0;i<160;i+=2) {
      for(unsigned channel=0;channel<2;++channel) {
        unsigned pos=unsigned(sample/2)+channel*(Overlay::Length/2);
        auto b=Overlay::byte(pos);auto& state=channel?right:left;
        expected[i*2+channel]=referenceSample(g721_decoder(b&15,&state));
        expected[(i+1)*2+channel]=referenceSample(g721_decoder(b>>4,&state));
      }
      sample+=2;if(sample==LoopEnd) sample=LoopStart;
    }
    assert(dmaCallback);dmaCallback();
    if(p.x18_curSamp!=sample || !dmaAddress || std::memcmp(reinterpret_cast<void*>(dmaAddress),expected.data(),640)) {
      std::fprintf(stderr,"mix pass=%u sample=%d actualsample=%d volume=%u\n",pass,sample,p.x18_curSamp,p.xc0_volume);
      auto* actual=reinterpret_cast<ushort*>(dmaAddress);for(unsigned i=0;i<320;++i)if(actual[i]!=expected[i]) {std::fprintf(stderr,"diff %u actual=%d expected=%d\n",i,short(actual[i]),short(expected[i]));break;}std::abort();
    }
  }
}
static void asyncAndOutOfOrder() {
  Overlay s(3);auto p=std::unique_ptr<CStaticAudioPlayer>(makePlayer());s.waitRead(0);
  assert(p->x38_dvdRequests.size()==3 && !p->IsReady());p->StartMixOut();assert(!dmaCallback);
  auto* last=static_cast<CRealDvdRequest*>(p->x38_dvdRequests[2].get());
  assert(aurora_dvd_prioritize_command(&last->FileInfo()->cb));
  s.release(0);s.waitRead(1);
  assert(last->IsComplete() && !p->IsReady());p->StartMixOut();assert(!dmaCallback);
  s.release(1);ready(*p);checkBytes(*p);
  for(unsigned i=0;i<10;++i) {p->StartMixOut();p->StartMixOut();checkMix(*p);p->StopMixOut();p->StopMixOut();assert(!dmaCallback && p->IsReady());}
  p.reset();
}
static void gatedCallback(s32 result,DVDFileInfo* info) {
  CDvdFile::internalCallback(result,info);
  DCInvalidateRange(info->cb.addr,info->cb.length);
}
static void destroyHeld(bool callback) {
  Overlay s(1);s.holdCallback=callback;
  auto* p=makePlayer();s.waitRead(0);
  if(callback) {
    static_cast<CRealDvdRequest*>(p->x38_dvdRequests[0].get())->FileInfo()->callback=gatedCallback;
    s.release(0);s.waitCallback();
  }
  const unsigned before=frees;std::atomic_bool entered=false;
  auto destroyed=std::async(std::launch::async,[&]{entered=true;delete p;});
  while(!entered)std::this_thread::yield();
  assert(destroyed.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
  assert(frees==before && liveAllocations==5);
  if(callback)s.releaseCallback();else s.release(0);
  destroyed.get();
}
static void startHeldCallback() {
  Overlay s(1);s.holdCallback=true;
  auto p=std::unique_ptr<CStaticAudioPlayer>(makePlayer());s.waitRead(0);
  static_cast<CRealDvdRequest*>(p->x38_dvdRequests[2].get())->FileInfo()->callback=gatedCallback;
  s.release(0);s.waitCallback();assert(p->IsReady());
  auto started=std::async(std::launch::async,[&]{p->StartMixOut();});
  assert(started.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout && !dmaCallback);
  s.releaseCallback();started.get();assert(dmaCallback);checkBytes(*p);checkMix(*p);p->StopMixOut();p.reset();
}
static void partialFailure(bool allocation) {
  Overlay s(1);if(!allocation)s.failOpen=4;
  auto constructed=std::async(std::launch::async,[&]{if(allocation) {failGame=4;beforeGameFailure=[] {Overlay::current->waitRead(0);};}
    bool failed=false;try{delete makePlayer();}catch(const std::exception&){failed=true;}failGame=-1;beforeGameFailure=nullptr;return failed;});
  s.waitRead(0);
  assert(constructed.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
  assert(writer && liveAllocations>=3);s.release(0);assert(constructed.get());
}
static void badRead(int result,bool cancelled=false) {
  Overlay s(cancelled?1:0);s.badOffset=Overlay::Chunk;s.badResult=result;
  auto p=std::unique_ptr<CStaticAudioPlayer>(makePlayer());
  if(cancelled) {s.waitRead(0);p->x38_dvdRequests[1]->PostCancelRequest();s.release(0);}
  bool failed=false;
  for(unsigned i=0;i<1000000 && !failed;++i) {
    try {if(p->IsReady())std::abort();p->StartMixOut();}
    catch(const std::runtime_error&){failed=true;}
    std::this_thread::yield();
  }
  assert(failed && !dmaCallback);p.reset();
}
static void allocationFailures() {
  // Fail every main-thread host allocation in the real construction path,
  // including rstl reserves, request objects, open handles and worker enqueue.
  unsigned failures=0;
  for(int n=0;n<40;++n) {
    Overlay s;failNew=n;CStaticAudioPlayer* p=nullptr;
    try {p=makePlayer();}catch(const std::bad_alloc&){++failures;}
    failNew=-1;delete p;
  }
  assert(failures>=10);
  for(int n=0;n<5;++n) {Overlay s;failGame=n;bool failed=false;
    try{delete makePlayer();}catch(const std::bad_alloc&){failed=true;}failGame=-1;assert(failed);}
}
int main(int argc,char** argv) {
  assert(argc==2 && aurora_dvd_open(argv[1]));DVDInit();
  asyncAndOutOfOrder();destroyHeld(false);destroyHeld(true);startHeldCallback();
  partialFailure(false);partialFailure(true);
  badRead(-1);badRead(0);badRead(Overlay::Chunk-1);badRead(0,true);
  allocationFailures();aurora_dvd_close();
  std::puts("static_audio: async/out_of_order cancellation callback_drain partial_unwind allocation_failure short/error/cancel PCM_loop_restart=ok");
}
