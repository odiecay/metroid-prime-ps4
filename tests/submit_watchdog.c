// SPDX-License-Identifier: MIT
#include "submit_watchdog.h"
#include <assert.h>
#include <stdio.h>
#define MS (1000ull*1000)

int main(void) {
   struct PrimeSubmitWatchdog w={0};
   /* Four submissions in a healthy frame, and even a much larger CPU burst,
    * cannot trigger a dump just because a GPU write has not arrived yet. */
   for(uint64_t i=0;i<100000;++i)
      assert(!prime_submit_watchdog_poll(&w,12772,12773+i,i*10));
   assert(!prime_submit_watchdog_poll(&w,12773,12777,8*MS));
   assert(!prime_submit_watchdog_poll(&w,12777,12777,16*MS));

   w=(struct PrimeSubmitWatchdog){0};
   assert(!prime_submit_watchdog_poll(&w,1,1,0));
   assert(!prime_submit_watchdog_poll(&w,1,1,1000*MS)); // idle is not a stall
   assert(!prime_submit_watchdog_poll(&w,1,2,1000*MS));
   assert(!prime_submit_watchdog_poll(&w,1,4,1249*MS));
   assert(prime_submit_watchdog_poll(&w,1,4,1250*MS));
   assert(!prime_submit_watchdog_poll(&w,1,16,2000*MS)); // once until progress
   assert(!prime_submit_watchdog_poll(&w,2,4,2001*MS));
   assert(prime_submit_watchdog_poll(&w,2,4,2251*MS));

   assert(!prime_submit_watchdog_poll(&w,2,4,10*MS)); // backwards clock resets
   assert(!prime_submit_watchdog_poll(&w,2,4,259*MS));
   assert(prime_submit_watchdog_poll(&w,2,4,260*MS));
   assert(!prime_submit_watchdog_poll(&w,2,4,UINT64_MAX)); // failed clock
   assert(!prime_submit_watchdog_poll(&w,2,4,100*MS));
   assert(!prime_submit_watchdog_poll(&w,2,4,349*MS));
   assert(prime_submit_watchdog_poll(&w,2,4,350*MS));

   w=(struct PrimeSubmitWatchdog){0};
   const uint64_t submitted=(1ull<<32)+2;
   assert(!prime_submit_watchdog_poll(&w,0xfffffffeu,submitted,0));
   assert(prime_submit_watchdog_poll(&w,0xfffffffeu,submitted,250*MS));
   assert(!prime_submit_watchdog_poll(&w,2,submitted,251*MS)); // wrapped label retired
   assert(!prime_submit_watchdog_poll(&w,2,submitted+1,1000*MS));
   assert(prime_submit_watchdog_poll(&w,2,submitted+1,1250*MS));

   assert(!prime_submit_full_dump_requested(NULL));
   assert(!prime_submit_full_dump_requested(""));
   assert(!prime_submit_full_dump_requested("0"));
   assert(!prime_submit_full_dump_requested("10"));
   assert(prime_submit_full_dump_requested("1"));
   puts("submit watchdog: rapid healthy submits, elapsed stalls, bounded reporting, progress, clocks, sequence wrap and explicit dumps passed");
   return 0;
}
