// SPDX-License-Identifier: MIT
// Existing real GX/FIFO tests plus deterministic wake-registration edges.
#include "../game/MetroidPrimePort/extern/aurora/tests/gx_fifo_test.cpp"

namespace {
std::atomic<unsigned> wake64BlockStage{};
std::atomic<bool> wake64Entered{},wake64Release{};
struct Wake64ReleaseGuard {
 ~Wake64ReleaseGuard(){wake64BlockStage.store(0);wake64Release.store(true);wake64Release.notify_all();}
};
}
extern "C" void prime_fifo_wake64_hook(unsigned stage) noexcept {
 auto expected=stage;
 if(wake64BlockStage.compare_exchange_strong(expected,0)) {
  wake64Entered.store(true);wake64Entered.notify_all();
  wake64Release.wait(false);
 }
}
extern "C" void prime_fifo_wake64_set_stream(uint64_t);
TEST_F(GXFifoTest, Native64WatermarkRegistrationRacesPreserveCommands) {
 namespace fifo=aurora::gx::fifo;
 for(unsigned stage:{1u,2u,3u})for(unsigned iteration=0;iteration<50;++iteration){
  Wake64ReleaseGuard cleanup;wake64BlockStage.store(0);wake64Release=true;
  fifo::init();fifo::begin_frame();
  wake64Entered=false;wake64Release=false;wake64BlockStage=stage;
  GXSetDrawSync(u16(1000+iteration));
  ASSERT_TRUE(wait_for(wake64Entered,true));
  GXSetDrawSync(u16(2000+iteration));
  wake64Release.store(true);wake64Release.notify_all();
  fifo::drain();EXPECT_EQ(GXReadDrawSync(),u16(2000+iteration));fifo::end_frame();fifo::shutdown();
 }
}
TEST_F(GXFifoTest, Native64IdleStopChangesValueAndProcessesPublishedTail) {
 namespace fifo=aurora::gx::fifo;
 for(unsigned stage:{1u,2u,3u})for(unsigned iteration=0;iteration<20;++iteration){
  Wake64ReleaseGuard cleanup;wake64BlockStage.store(0);wake64Release=true;
  fifo::init();fifo::begin_frame();wake64Entered=false;wake64Release=false;wake64BlockStage=stage;
  GXSetDrawSync(u16(3000+iteration));ASSERT_TRUE(wait_for(wake64Entered,true));
  std::atomic<bool> stopping{},finished{};
  std::thread stopper{[&]{stopping=true;fifo::shutdown();finished=true;}};
  EXPECT_TRUE(wait_for(stopping,true));EXPECT_FALSE(finished.load());
  wake64Release.store(true);wake64Release.notify_all();stopper.join();
  EXPECT_TRUE(finished.load());EXPECT_EQ(GXReadDrawSync(),u16(3000+iteration));
  fifo::drain();fifo::end_frame();
 }
}
TEST_F(GXFifoTest, Native64SignedBoundaryRetainsAbsoluteWatermarkAndWakeStatistic) {
 namespace fifo=aurora::gx::fifo;
 for(uint64_t base:{(uint64_t(1)<<32)-2,(uint64_t(1)<<63)-2,(uint64_t(1)<<63)+100}){
  fifo::init();prime_fifo_wake64_set_stream(base);fifo::begin_frame();
  for(u16 token=1;token<=1000;++token)GXSetDrawSync(token);
  fifo::drain();EXPECT_EQ(GXReadDrawSync(),1000);EXPECT_LT(fifo::wake_count(),10000u);
  fifo::end_frame();fifo::shutdown();
 }
}
