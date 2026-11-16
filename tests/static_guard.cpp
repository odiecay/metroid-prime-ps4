// Exercise the actual patched LLVM guard, with the SDK's broken syscall alias.
#include <memory> // Host libc++ headers, before selecting the native guard branch.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#undef SYS_gettid
#define SYS_gettid SYS_getpid
#define __ORBIS__ 1
#define TESTING_CXA_GUARD
#include "cxa_guard_impl.h"

static thread_local int native_id;
static unsigned id_source=2;
static unsigned id_calls;
extern "C" int scePthreadGetthreadid(void) {
  __atomic_fetch_add(&id_calls,1,__ATOMIC_RELAXED);
  if (!native_id) __builtin_trap();
  return native_id;
}

static uint64_t guard;
static unsigned initializers;
static unsigned value;
static pthread_barrier_t start;
static void* contend(void*) {
  native_id=__atomic_fetch_add(&id_source,1,__ATOMIC_RELAXED);
  pthread_barrier_wait(&start);
  __cxxabiv1::SelectedImplementation state(&guard);
  if (state.cxa_guard_acquire()==__cxxabiv1::AcquireResult::INIT_IS_PENDING) {
    __atomic_fetch_add(&initializers,1,__ATOMIC_RELAXED);
    usleep(5000);
    value=0x12345678;
    state.cxa_guard_release();
  }
  if (value!=0x12345678) __builtin_trap();
  return nullptr;
}
int main() {
  native_id=1;
  for (unsigned round=0;round<40;++round) {
    guard=0;value=0;initializers=0;
    pthread_t workers[8];
    if (pthread_barrier_init(&start,nullptr,8)) return 1;
    for (auto& worker:workers) if(pthread_create(&worker,nullptr,contend,nullptr)) return 2;
    for (auto worker:workers) if(pthread_join(worker,nullptr)) return 3;
    if(initializers!=1) return 4;
    pthread_barrier_destroy(&start);
  }
  uint64_t retry=0;
  __cxxabiv1::SelectedImplementation aborted(&retry);
  if(aborted.cxa_guard_acquire()!=__cxxabiv1::AcquireResult::INIT_IS_PENDING) return 5;
  aborted.cxa_guard_abort();
  if(aborted.cxa_guard_acquire()!=__cxxabiv1::AcquireResult::INIT_IS_PENDING) return 6;
  aborted.cxa_guard_release();
  if(aborted.cxa_guard_acquire()!=__cxxabiv1::AcquireResult::INIT_IS_DONE) return 7;
  const pid_t child=fork();
  if(child<0) return 8;
  if(child==0) {
    uint64_t recursive=0;
    __cxxabiv1::SelectedImplementation state(&recursive);
    state.cxa_guard_acquire();state.cxa_guard_acquire();_exit(10);
  }
  int status=0;
  if(waitpid(child,&status,0)!=child || !WIFSIGNALED(status) || WTERMSIG(status)!=SIGABRT) return 9;
  if(id_calls==0) return 11;
  puts("40 eight-thread races, retry after abort and true recursion detection passed");
}
