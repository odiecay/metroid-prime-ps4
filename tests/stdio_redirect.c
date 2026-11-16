// SPDX-License-Identifier: MIT
// Host test of the actual native compatibility function and real descriptors.
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

extern int __wrap___dup3(int,int,int);
extern int prime_ps4_log_open(const char *);
extern int prime_ps4_log_active(void);
extern ssize_t __wrap__write(int,const void *,size_t);
extern ssize_t __wrap__writev(int,const struct iovec *,int);
ssize_t __real__write(int fd,const void *data,size_t size) { return write(fd,data,size); }
ssize_t __real__writev(int fd,const struct iovec *v,int count) { return writev(fd,v,count); }
static unsigned delegated;
int __real___dup3(int oldfd,int newfd,int flags) {
    (void)oldfd;(void)newfd;(void)flags;
    ++delegated;errno=ENOSYS;return -1;
}
int main(void) {
    char path[]="/tmp/prime-stdio-XXXXXX";
    int file=mkstemp(path);assert(file>=0);unlink(path);
    int first=dup(file),second=dup(file);assert(first>=0 && second>=0);
    assert(__wrap___dup3(file,first,0)==first);
    assert(__wrap___dup3(file,second,0)==second);
    assert(write(first,"stdout\n",7)==7);
    assert(write(second,"stderr\n",7)==7);
    assert(write(first,"stdout-again\n",13)==13);
    assert(lseek(file,0,SEEK_SET)==0);
    char data[64]={0};assert(read(file,data,sizeof(data))==27);
    assert(!strcmp(data,"stdout\nstderr\nstdout-again\n"));
    assert(__wrap___dup3(file,file,0)==-1 && errno==EINVAL);
    assert(__wrap___dup3(-1,first,0)==-1 && errno==EBADF);
    assert(__wrap___dup3(file,first,1)==-1 && errno==ENOSYS && delegated==1);
    char log_path[]="/tmp/prime-routed-log-XXXXXX";
    int empty=mkstemp(log_path);assert(empty>=0);close(empty);
    assert(!prime_ps4_log_active());
    assert(prime_ps4_log_open("/no-such-prime-parent/log")<0);
    assert(!prime_ps4_log_active());
    assert(prime_ps4_log_open(log_path)==0);
    assert(prime_ps4_log_active());
    assert(prime_ps4_log_open(log_path)==0);
    assert(__wrap__write(STDOUT_FILENO,"stdout\n",7)==7);
    struct iovec pieces[]={{"stderr",6},{"\n",1}};
    assert(__wrap__writev(STDERR_FILENO,pieces,2)==7);
    assert(__wrap__write(STDOUT_FILENO,"stdout-again\n",13)==13);
    int log=open(log_path,O_RDONLY);assert(log>=0);unlink(log_path);
    memset(data,0,sizeof(data));assert(read(log,data,sizeof(data))==27);
    assert(!strcmp(data,"stdout\nstderr\nstdout-again\n"));close(log);
    assert(__wrap__write(file,"other",5)==5);
    assert(lseek(file,27,SEEK_SET)==27);
    memset(data,0,sizeof(data));assert(read(file,data,sizeof(data))==5);
    assert(!strcmp(data,"other"));
    assert(__wrap__write(-1,"x",1)==-1 && errno==EBADF);
    close(first);close(second);close(file);
    puts("native stdio fallback: shared offsets and failures passed");
}
