// Execute the actual native constructor runner with scalar kernel I/O seams.
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned opens, closes, writes, debug_lines, constructors, init_calls;
static unsigned fail_opens, chunk, write_failure, nested;
static unsigned flags[1024], positions[1024], active[1024];
static unsigned file_size;
static char contents[16384], last_debug[512];

int sceKernelOpen(const char* path, int mode, int perms) {
    assert(!strcmp(path,"/data/project-prime/logs/startup.log"));
    assert(perms==0666);
    ++opens;
    if(opens<=fail_opens) return -1;
    unsigned fd=opens+16;assert(fd<1024);
    active[fd]=1;flags[fd]=(unsigned)mode;positions[fd]=0;
    if(mode&O_TRUNC) {file_size=0;contents[0]=0;}
    return (int)fd;
}
int64_t sceKernelWrite(int fd,const void* data,size_t size) {
    assert(fd>=0 && fd<1024 && active[fd]);++writes;
    if(write_failure) return write_failure==1?0:-5;
    if(chunk && size>chunk)size=chunk;
    if(flags[fd]&O_APPEND)positions[fd]=file_size;
    assert(positions[fd]+size<sizeof(contents));
    memcpy(contents+positions[fd],data,size);positions[fd]+=(unsigned)size;
    if(positions[fd]>file_size)file_size=positions[fd];
    contents[file_size]=0;
    return (int64_t)size;
}
int sceKernelClose(int fd) {assert(fd>=0 && fd<1024 && active[fd]);active[fd]=0;++closes;return 0;}
int sceKernelDebugOutText(int kind,const char* text) {
    assert(kind==0);++debug_lines;snprintf(last_debug,sizeof(last_debug),"%s",text);return 0;
}
int sceKernelMkdir(const char* path,int mode) {(void)path;assert(mode==0777);return 0;}
int sceKernelRename(const char* a,const char* b) {(void)a;(void)b;return 0;}
int sceKernelFsync(int fd) {(void)fd;return 0;}
int sceKernelUsleep(unsigned us) {(void)us;return 0;}
int sceSystemServiceHideSplashScreen(void) {return 0;}

void prime_ps4_startup_stage(const char*);
static void test_init(void) {
    assert(!strcmp(last_debug,"Project Prime: C runtime reached; beginning constructors\n"));
    assert(constructors==0);++init_calls;
}
static void ctor(void) {
    assert(init_calls==1);
    assert(!strncmp(last_debug,"constructor image-offset=0x",26));
    ++constructors;
    if(nested && constructors==1)prime_ps4_startup_stage("nested stage\n");
}
typedef void (*test_initializer)(void);
test_initializer fixture_start[4]={ctor,ctor,ctor,ctor};
__asm__(".global fixture_end\n.set fixture_end, fixture_start + 32");
unsigned char fixture_image[1];
#define _init test_init
#define __init_array_start fixture_start
#define __init_array_end fixture_end
#define __text_start fixture_image
#define __libc_start_init fixture_run
#include "../platform/ps4/startup.c"

static void run(unsigned failed,unsigned part,unsigned failed_write,unsigned reentrant) {
    opens=closes=writes=debug_lines=constructors=init_calls=file_size=0;
    memset(flags,0,sizeof(flags));memset(positions,0,sizeof(positions));memset(active,0,sizeof(active));
    contents[0]=last_debug[0]=0;
    fail_opens=failed;chunk=part;write_failure=failed_write;nested=reentrant;
    fixture_run();
    assert(constructors==4 && init_calls==1 && debug_lines==6+reentrant);
    assert(!strcmp(last_debug,"Project Prime: all constructors completed\n"));
    for(unsigned i=0;i<1024;++i)assert(!active[i]);
    if(!failed)assert(opens==1+reentrant && closes==opens);
    if(!failed && !failed_write) {
        assert(strstr(contents,"C runtime reached"));
        assert(strstr(contents,"all constructors completed"));
        unsigned count=0;const char* p=contents;
        while((p=strstr(p,"constructor image-offset="))!=NULL){++count;++p;}
        assert(count==4);
        if(reentrant)assert(strstr(contents,"nested stage\n"));
        if(!part)assert(writes==6+reentrant);
    }
    if(failed==1)assert(opens==7 && closes==6);
    if(failed>100)assert(opens==7 && closes==0 && writes==0);
    if(!failed && failed_write)assert(writes==6+reentrant);
}
int main(void) {
    run(0,0,0,0);run(0,0,0,1);run(0,3,0,1);run(0,1,0,0);
    run(1,0,0,0);run(1000,0,0,0);run(0,0,1,0);run(0,0,2,0);
    puts("startup constructor order, breadcrumbs, one owned fd, nested stages, short writes and failures passed");
}
