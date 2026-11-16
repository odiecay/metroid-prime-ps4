// SPDX-License-Identifier: MIT
// The SDK's musl freopen calls __dup3 even without O_CLOEXEC. Native dup2
// implements that case correctly; leave flagged requests to the SDK backend.
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/uio.h>
#include <unistd.h>

_Static_assert(ATOMIC_INT_LOCK_FREE==2,"Native log routing must be signal safe");
static _Atomic int log_fd=-1;
int prime_ps4_log_active(void) { return atomic_load(&log_fd)>=0; }

int prime_ps4_log_open(const char *path) {
    int expected=-1;
    if(!atomic_compare_exchange_strong(&log_fd,&expected,-2)) {
        if(expected>=0) return 0;
        errno=EBUSY;return -1;
    }
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    atomic_store(&log_fd,fd<0?-1:fd);
    return fd<0?-1:0;
}

static int destination(int fd) {
    if(fd==STDOUT_FILENO || fd==STDERR_FILENO) {
        const int log=atomic_load(&log_fd);
        if(log>=0) return log;
    }
    return fd;
}

extern ssize_t __real__write(int fd,const void *data,size_t size);
extern ssize_t __real__writev(int fd,const struct iovec *vectors,int count);
ssize_t __wrap__write(int fd,const void *data,size_t size) {
    return __real__write(destination(fd),data,size);
}
ssize_t __wrap__writev(int fd,const struct iovec *vectors,int count) {
    return __real__writev(destination(fd),vectors,count);
}

extern int __real___dup3(int oldfd,int newfd,int flags);
int __wrap___dup3(int oldfd,int newfd,int flags) {
    if(flags) return __real___dup3(oldfd,newfd,flags);
    if(oldfd==newfd) { errno=EINVAL;return -1; }
    return dup2(oldfd,newfd);
}
