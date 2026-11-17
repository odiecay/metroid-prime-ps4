#include <aurora/frame_trace.hpp>
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static unsigned retiredControls;
struct Control { ~Control() { ++retiredControls; } };
class CDvdFile {
public:
  bool mARAMAllocated=false, mARAMPopped=false, mARAMFailed=false;
  std::unique_ptr<Control> mARAMFile;
  std::string mFilename="aram:test";
  unsigned checks=0, polls=0, yields=0, remaining=0;
  uint64_t firstCheckStart=0, firstCheckEnd=0;
  std::atomic_bool checking{false};
  bool ActualIsARAMFileLoaded();
  bool IsARAMFileLoaded() {
    ++checks;
    if (checks==1) firstCheckStart=aurora::trace::now_ns();
    checking.store(true,std::memory_order_release);
    const bool result=ActualIsARAMFileLoaded();
    if (checks==1) firstCheckEnd=aurora::trace::now_ns();
    return result;
  }
  void StallForARAMFile();
};

// These include the actual locked readiness predicate and stall bodies. Only
// the transfer-pump providers are controlled; no I/O duration is simulated.
#include "actual_aram_mutex.inc"
#include "actual_aram_ready.inc"
static CDvdFile* active;
static void ARQPoll() {
  ++active->polls;
  if (active->remaining && --active->remaining==0) {
    std::lock_guard lock(AramFileStateMutex());
    active->mARAMPopped=true;
  }
}
static void OSYieldThread() { ++active->yields; }
#include "actual_aram_stall.inc"

static void run(unsigned kind) {
  CDvdFile file; active=&file;
  file.mARAMAllocated=kind!=0;
  file.mARAMPopped=kind==1 || kind==3 || kind==5;
  file.mARAMFailed=kind==4 || kind==5;
  file.remaining=kind==2 ? 3 : kind==4 ? 2 : 0;
  const bool hasControl=kind==2 || kind==3 || kind==4 || kind==6;
  if (hasControl) file.mARAMFile=std::make_unique<Control>();
  const auto retired=retiredControls;
  for (unsigned slot=0;slot<aurora::trace::MaxThreads;++slot)
    aurora::trace::drain(slot,[](const auto&) {});
  std::atomic_bool held=false;
  std::thread transfer;
  if (kind==6) {
    transfer=std::thread([&] {
      std::lock_guard lock(AramFileStateMutex());
      held.store(true,std::memory_order_release);
      while (!file.checking.load(std::memory_order_acquire)) std::this_thread::yield();
      file.mARAMPopped=true;
    });
    while (!held.load(std::memory_order_acquire)) std::this_thread::yield();
  }
  const uint64_t frame=0x4152414d0000ULL+kind;
  bool failed=false;
  {
    aurora::trace::Frame caller(aurora::trace::Main,frame);
    try { file.StallForARAMFile(); }
    catch (const std::runtime_error& error) {
      failed=true;
      assert(std::string(error.what())=="Failed to load ARAM file: aram:test");
    }
  }
  if (transfer.joinable()) transfer.join();
  assert(failed==file.mARAMFailed);
  const unsigned loops=kind==2 ? 3 : kind==4 ? 2 : 0;
  assert(file.polls==loops && file.yields==loops && file.checks==loops+1);
  assert(!file.mARAMFile && retiredControls-retired==unsigned(hasControl));
  unsigned intervals=0;
  for (unsigned slot=0;slot<aurora::trace::MaxThreads;++slot) {
    aurora::trace::drain(slot,[&](const aurora::trace::Event& event) {
      if (!event.name || std::strcmp(event.name,"ARAM file preload readiness (DVD/ARQ inclusive)")) return;
      ++intervals;
      assert(event.frame==frame && event.lane==aurora::trace::Main && event.kind==aurora::trace::Inclusive);
      assert(event.startNs<=file.firstCheckStart && event.startNs+event.value>=file.firstCheckEnd);
    });
  }
  assert(intervals==unsigned(hasControl));
  if (hasControl && !failed) {
    {
      aurora::trace::Frame again(aurora::trace::Main,frame+0x100);
      file.StallForARAMFile();
    }
    unsigned repeated=0;
    for (unsigned slot=0;slot<aurora::trace::MaxThreads;++slot)
      aurora::trace::drain(slot,[&](const aurora::trace::Event& event) {
        if (event.name && !std::strcmp(event.name,"ARAM file preload readiness (DVD/ARQ inclusive)")) ++repeated;
      });
    assert(repeated==0 && retiredControls-retired==1);
    assert(file.polls==loops && file.yields==loops && file.checks==loops+2);
  }
}

int main() {
  for (unsigned kind=0;kind<7;++kind) run(kind);
}
