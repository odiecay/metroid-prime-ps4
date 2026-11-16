// SPDX-License-Identifier: MIT
// Trace native C/C++ initialization without depending on C++ or heap state.
// Override musl's weak constructor runner, preserving its _init + array order.
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <unistd.h>

typedef void (*initializer)(void);
extern initializer __init_array_start[], __init_array_end[];
extern unsigned char __text_start[];
extern void _init(void);

static void raw_line(const char *text, int truncate) {
    size_t length=0;
    while(text[length]) ++length;
    sceKernelDebugOutText(0,text);
    int fd=sceKernelOpen("/data/project-prime/logs/startup.log",
                         O_WRONLY|O_CREAT|(truncate?O_TRUNC:O_APPEND),0666);
    if(fd<0) return;
    size_t done=0;
    while(done<length) {
        size_t n=sceKernelWrite(fd,text+done,length-done);
        if(n==0 || n>length-done) break;
        done+=n;
    }
    sceKernelClose(fd);
}

void prime_ps4_startup_stage(const char *stage) { raw_line(stage,0); }

void __libc_start_init(void) {
    sceKernelMkdir("/data/project-prime",0777);
    sceKernelMkdir("/data/project-prime/logs",0777);
    sceKernelRename("/data/project-prime/logs/startup.log",
                    "/data/project-prime/logs/startup.previous.log");
    raw_line("Project Prime: C runtime reached; beginning constructors\n",1);
    _init();
    for(initializer *p=__init_array_start;p<__init_array_end;++p) {
        char line[]="constructor image-offset=0x0000000000000000\n";
        uintptr_t offset=(uintptr_t)*p-(uintptr_t)__text_start;
        for(unsigned i=0;i<16;++i) {
            line[sizeof("constructor image-offset=0x")-1+15-i]=
                "0123456789abcdef"[offset&15];offset>>=4;
        }
        raw_line(line,0);
        (*p)();
    }
    raw_line("Project Prime: all constructors completed\n",0);
}

// A separate diagnostic package selects this entry in the SAME linked game
// image. Normal game packages keep the SDK CRT entry. No game logic is mocked.
__attribute__((used,retain,visibility("default"),noreturn))
void prime_ps4_loader_entry(void *arguments) {
    (void)arguments;
    static const char report[]="{\"schema\":1,\"native_entry_reached\":true,\"c_runtime_entered\":false}\n";
    sceKernelMkdir("/data/project-prime",0777);
    int fd=sceKernelOpen("/data/project-prime/loader.jsonl",O_WRONLY|O_CREAT|O_TRUNC,0666);
    if(fd>=0) {
        size_t done=0;
        while(done<sizeof(report)-1) {
            size_t n=sceKernelWrite(fd,report+done,sizeof(report)-1-done);
            if(n==0 || n>sizeof(report)-1-done) break;
            done+=n;
        }
        sceKernelFsync(fd);
        sceKernelClose(fd);
    }
    sceKernelDebugOutText(0,"Project Prime: raw native entry reached, C/C++ startup bypassed for loader diagnostic\n");
    sceKernelUsleep(3000000);
    sceSystemServiceHideSplashScreen();
    sceKernelUsleep(1000000);
    _exit(0);
}
