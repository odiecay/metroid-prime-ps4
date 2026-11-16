// SPDX-License-Identifier: MIT
// Bounded native pthread wait channels for libc++ atomic wait/notify. This
// remains separate from Mesa's private umtx table and exports no umtx symbols.
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <cstdlib>

namespace prime::runtime {
struct AtomicWaitBucket {
    pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
};
inline AtomicWaitBucket &atomic_wait_bucket(const volatile void *address) {
    static AtomicWaitBucket buckets[64];
    return buckets[(reinterpret_cast<uintptr_t>(address)>>3)&63];
}
template<class Predicate>
void atomic_wait(const volatile void *address,Predicate still_equal) {
    auto &bucket=atomic_wait_bucket(address);
    if (pthread_mutex_lock(&bucket.mutex)) std::abort();
    // Test while holding the channel lock. pthread_cond_wait atomically joins
    // the wait set and releases it, so a notifier cannot slip between them.
    while (still_equal())
        if (pthread_cond_wait(&bucket.condition,&bucket.mutex)) std::abort();
    if (pthread_mutex_unlock(&bucket.mutex)) std::abort();
}
inline void atomic_notify(const volatile void *address) {
    auto &bucket=atomic_wait_bucket(address);
    if (pthread_mutex_lock(&bucket.mutex)) std::abort();
    // Colliding addresses may share a channel. Signaling just one could wake
    // another address's waiter and leave the intended waiter asleep forever.
    // Broadcast permits spurious wakes; each waiter checks its own predicate.
    if (pthread_cond_broadcast(&bucket.condition)) std::abort();
    if (pthread_mutex_unlock(&bucket.mutex)) std::abort();
}
}
