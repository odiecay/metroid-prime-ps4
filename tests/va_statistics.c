#include "../renderer/orbis/va_statistics.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct PrimeOrbisVaWalk w;
    assert(prime_va_begin(&w,100,1100));prime_va_finish(&w);
    assert(w.stats.capacity==1000 && w.stats.free==1000 && w.stats.largest_free==1000);
    assert(prime_va_begin(&w,100,1100));
    assert(prime_va_add(&w,100,200,false));
    assert(prime_va_add(&w,400,100,true));
    assert(prime_va_add(&w,800,200,false));
    prime_va_finish(&w);
    assert(w.stats.live==400 && w.stats.retired==100 && w.stats.free==500);
    assert(w.stats.largest_free==300 && w.stats.ranges==3 && w.stats.retired_ranges==1);
    // A large total free count alone does not guarantee a contiguous allocation.
    assert(w.stats.free>=400 && w.stats.largest_free<400);
    // Once retired ranges are actually removed by the driver, the gap is reusable.
    assert(prime_va_begin(&w,100,1100));
    assert(prime_va_add(&w,100,200,false));assert(prime_va_add(&w,800,200,false));
    prime_va_finish(&w);assert(w.stats.free==600 && w.stats.largest_free==500);
    assert(!prime_va_begin(&w,100,99));
    assert(prime_va_begin(&w,UINT64_MAX-100,UINT64_MAX));
    assert(!prime_va_add(&w,UINT64_MAX-50,100,false));
    assert(prime_va_add(&w,UINT64_MAX-100,50,false));
    assert(!prime_va_add(&w,UINT64_MAX-75,10,false));
    prime_va_finish(&w);assert(w.stats.free==50 && w.stats.largest_free==50);
    puts("VA accounting: live/retired, fragmentation, reclamation and overflow checks passed");
}
