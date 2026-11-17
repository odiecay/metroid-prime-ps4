#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
extern "C" void* prime_sdk_memcpy(void*,const void*,size_t);
#include "fifo_small_copy.hpp"
using namespace aurora::gx::fifo::detail;
static void policy_contract() {
 FifoSmallCopyCounters stats;
 FifoSmallCopyPreflight small{true};
 for(uint32_t n:{8u,32u,40u,48u,56u,96u,192u,256u})small.add(n);
 assert(small.allSmall);
 const std::array<uint32_t,8> expected{1,1,1,1,1,1,2,0};assert(small.lengths==expected);
 stats.record(small,728,true);assert(stats.eligibleBatches==1&&stats.smallBatches==1&&stats.smallBytes==728);
 auto failedAttempt=small;failedAttempt.add(257);assert(!failedAttempt.allSmall&&small.allSmall);
 // No record call for rejected mapping/preflight: no published attempt counts.
 assert(stats.fallbackBatches==0&&stats.lengths[7]==0);
 stats.record(small,728,false);assert(stats.eligibleBatches==2&&stats.smallBatches==1&&stats.fallbackBatches==1);
 stats.record(failedAttempt,985,true);assert(stats.eligibleBatches==2&&stats.fallbackBatches==2&&stats.fallbackBytes==1713);
 assert(stats.lengths[3]==3&&stats.lengths[7]==1);
 FifoSmallCopyPreflight quiet{false};quiet.add(48);quiet.add(257);assert(!quiet.allSmall);
 auto before=stats;stats.record(quiet,305,true);assert(!std::memcmp(&stats,&before,sizeof(stats)));
 const auto snapshot=stats.take();assert(snapshot.smallBatches==1&&snapshot.fallbackBatches==2);
 assert(stats.eligibleBatches==0&&stats.smallBatches==0&&stats.fallbackBatches==0);
 for(auto n:stats.lengths)assert(n==0);
 for(uint32_t n=0;n<=1024;++n){
  unsigned bin=n<=16?0:n<=32?1:n<48?2:n==48?3:n<=64?4:n<=128?5:n<=256?6:7;
  assert(small_copy_bin(n)==bin);
 }
 auto one=FifoSmallCopyPreflight::one(48,true);assert(one.allSmall&&one.collect&&one.lengths[3]==1);
}
int main(){
 policy_contract();
 uint64_t alignCases=0,guardCases=0,fallbackCases=0;
 for(size_t n=0;n<=256;++n)for(size_t sa=0;sa<32;++sa)for(size_t da=0;da<32;++da){
  auto* src=new uint8_t[n+sa+1];auto* dst=new uint8_t[n+da+33];auto* ref=new uint8_t[n+da+33];
  for(size_t i=0;i<n+sa+1;++i)src[i]=uint8_t(i*19+n*31+sa);
  std::memset(dst,0xa5,n+da+33);std::memset(ref,0xa5,n+da+33);
  prime_sdk_memcpy(ref+da,src+sa,n);copy_small_fifo_payload(dst+da,src+sa,n);
  assert(!std::memcmp(dst,ref,n+da+33));++alignCases;
  delete[]src;delete[]dst;delete[]ref;
 }
 const size_t page=sysconf(_SC_PAGESIZE);
 auto allocate=[&](){void*p=mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);assert(!mprotect(static_cast<uint8_t*>(p)+page,page,PROT_READ|PROT_WRITE));return static_cast<uint8_t*>(p);};
 auto*s=allocate(),*d=allocate();
 for(size_t n=0;n<=256;++n)for(unsigned edges=0;edges<4;++edges){
  uint8_t*src=s+page+((edges&1)?page-n:0);uint8_t*dst=d+page+((edges&2)?page-n:0);
  for(size_t i=0;i<n;++i)src[i]=uint8_t(n+i*7);copy_small_fifo_payload(dst,src,n);
  assert(!std::memcmp(dst,src,n));++guardCases;
 }
 for(size_t n:{257u,511u,512u,513u,1024u,2048u,4096u}){
  uint8_t*src=s+2*page-n,*dst=d+2*page-n;
  for(size_t i=0;i<n;++i)src[i]=uint8_t(n+i*13);prime_sdk_memcpy(dst,src,n);assert(!std::memcmp(dst,src,n));++fallbackCases;
 }
 munmap(s,page*3);munmap(d,page*3);
 std::printf("{\"alignment_cases\":%llu,\"guard_page_cases\":%llu,\"sdk_large_fallbacks\":%llu,\"overlap_contract\":\"disjoint_source_and_destination_only_as_original_memcpy\"}\n",(unsigned long long)alignCases,(unsigned long long)guardCases,(unsigned long long)fallbackCases);
}
