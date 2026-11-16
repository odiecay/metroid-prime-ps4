// SPDX-License-Identifier: MIT
// Native application entry. Aurora still owns the real game/application loop.
#include <nod.h>
#include <aurora/main.h>
#undef main
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <orbis/CommonDialog.h>
#include <orbis/MsgDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <SDL3/SDL_hints.h>
#include "lifecycle.h"
#include "candidate_profile.hpp"
#include "benchmark_tour.hpp"
#include "asset_bundle.hpp"

extern "C" void prime_ps4_trace_start();
extern "C" void prime_ps4_trace_stop();
extern "C" void prime_ps4_startup_stage(const char *);
extern "C" int prime_ps4_sqlite_init(void);
extern "C" int prime_ps4_log_open(const char *);

namespace {
const char* boot_log_location() noexcept {
    const char* path=std::getenv("MP_PS4_BOOT_LOG");
    return path && *path ? path : "/data/project-prime/logs/boot.log";
}
void message(const char *text) {
    sceKernelDebugOutText(0,text);
    sceSystemServiceHideSplashScreen();
    int user=-1, params=256;
    sceUserServiceInitialize(&params);
    if (sceUserServiceGetInitialUser(&user)<0 || user<0) return;
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG);
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG);
    sceCommonDialogInitialize();
    if (sceMsgDialogInitialize()<0) return;
    OrbisMsgDialogUserMessageParam body{};
    body.buttonType=ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
    body.msg=text;
    OrbisMsgDialogParam dialog{};
    dialog.baseParam.size=sizeof(dialog.baseParam);
    dialog.baseParam.magic=uint32_t(uintptr_t(&dialog.baseParam)+ORBIS_COMMON_DIALOG_MAGIC_NUMBER);
    dialog.size=sizeof(dialog);
    dialog.mode=ORBIS_MSG_DIALOG_MODE_USER_MSG;
    dialog.userMsgParam=&body;
    dialog.userId=user;
    if (sceMsgDialogOpen(&dialog)>=0) {
        const uint64_t deadline=sceKernelGetProcessTime()+120000000;
        while (sceMsgDialogUpdateStatus()!=ORBIS_COMMON_DIALOG_STATUS_FINISHED &&
               sceKernelGetProcessTime()<deadline) sceKernelUsleep(10000);
        sceMsgDialogClose();
    }
    sceMsgDialogTerminate();
}
int64_t read_at(void *user,uint64_t offset,void *out,size_t size) {
    FILE *f=static_cast<FILE *>(user);
    if (offset>uint64_t(INT64_MAX) || fseeko(f,off_t(offset),SEEK_SET)) return -1;
    size_t n=fread(out,1,size,f);
    return ferror(f) ? -1 : int64_t(n);
}
int64_t length(void *user) {
    FILE *f=static_cast<FILE *>(user);
    return fseeko(f,0,SEEK_END) ? -1 : ftello(f);
}
void close_stream(void *user) { fclose(static_cast<FILE *>(user)); }
void cache_file_identity() {
    const char* folder=getenv("MP_CACHE_PATH");
    const std::string file=std::string(folder?folder:"/data/project-prime/cache")+"/dawn_cache.db";
    const char *path=file.c_str();
    struct stat byPath{},byHandle{};
    errno=0;
    const int pathResult=stat(path,&byPath),pathError=errno;
    const int fd=open(path,O_RDONLY);
    errno=0;
    const int handleResult=fd>=0?fstat(fd,&byHandle):-1,handleError=errno;
    if (fd>=0) close(fd);
    std::fprintf(stdout,
        "orbis: Dawn cache identity: stat=%d errno=%d dev=%llu ino=%llu size=%lld; "
        "fstat=%d errno=%d dev=%llu ino=%llu size=%lld\n",
        pathResult,pathError,(unsigned long long)byPath.st_dev,(unsigned long long)byPath.st_ino,
        (long long)byPath.st_size,handleResult,handleError,(unsigned long long)byHandle.st_dev,
        (unsigned long long)byHandle.st_ino,(long long)byHandle.st_size);
}
}

extern "C" void prime_ps4_report_fatal(const char *module,const char *text,unsigned length) {
    static std::atomic_flag active=ATOMIC_FLAG_INIT;
    if (active.test_and_set()) return;
    std::fflush(stdout);std::fflush(stderr);
    char explanation[1024];
    std::snprintf(explanation,sizeof(explanation),
        "Metroid Prime could not continue.\n\n%s: %.*s\n\nDetails are recorded in %s.",
        module?module:"Native engine",int(length>720?720:length),text?text:"",boot_log_location());
    message(explanation);
}

static int run_game(int argc,char **argv) {
    prime_ps4_startup_stage("Project Prime: main entered\n");
    const char *root="/data/project-prime";
    std::error_code ec;
    for (const char *dir:{"","/user","/cache","/data","/logs"}) {
        std::filesystem::create_directories(std::string(root)+dir,ec);
        if (ec) {
            message("Metroid Prime could not open its application storage. Free some space and launch again.");
            return 1;
        }
    }
    prime::CandidateProfile candidate;
    bool benchmarkTour=false;
    try {
        candidate=prime::read_candidate_profile("/app0/prime-profile.txt",root);
        prime::prepare_candidate_storage(candidate,"/data/project-prime/user");
        if(candidate.key=="consolidated" && !std::filesystem::exists(candidate.assetsRoot)) {
            prime_ps4_startup_stage("Project Prime: preparing preconverted Remastered graphics\n");
            const std::filesystem::path bundled="/app0/remastered.primebundle";
            prime::unpack_asset_bundle(std::filesystem::exists(bundled)?bundled:std::filesystem::path(candidate.assetsRoot.string()+".primebundle"),candidate.assetsRoot);
        }
        prime::apply_candidate_environment(candidate);
        benchmarkTour=prime::prepare_benchmark_tour(candidate.key.empty()?std::filesystem::path(root):candidate.root);
    } catch(const std::exception& error) {
        message(error.what());return 4;
    }
    const std::string logRoot=benchmarkTour?prime::benchmark_tour_log_root().string():
        candidate.key.empty()?"/data/project-prime/logs":(candidate.root/"logs").string();
    setenv("MP_PS4_BOOT_LOG",(logRoot+"/boot.log").c_str(),1);
    std::rename((logRoot+"/boot.log").c_str(),(logRoot+"/boot.previous.log").c_str());
    if (prime_ps4_log_open((logRoot+"/boot.log").c_str())<0) {
        char error[256];
        std::snprintf(error,sizeof(error),"Metroid Prime could not initialize its startup log (error %d).",errno);
        prime_ps4_startup_stage(error);
        message(error);
        return 1;
    }
    // Route the SDK's native _write/_writev calls through one ordinary handle.
    // Orbis rejects replacing the inherited standard descriptors with dup2.
    clearerr(stdout);clearerr(stderr);
    setvbuf(stdout,nullptr,_IOLBF,0);
    setvbuf(stderr,nullptr,_IOLBF,0);
    prime_ps4_startup_stage("Project Prime: application storage and boot log ready\n");
    setenv("MP_USER_PATH","/data/project-prime/user",0);
    setenv("MP_CACHE_PATH","/data/project-prime/cache",0);
    setenv("MP_DISC","/data/project-prime/data/MetroidPrime.iso",0);
    setenv("MP_GRAPHICS_BACKEND","vulkan",1);
    setenv("MP_NO_DISC_DIALOG","1",1);
    // Upstream's eager startup compiles the entire seed, costing about 950 MiB
    // on a measured low-memory device. Orbis has a fixed native GPU arena;
    // build each room's pipelines on demand and retire long-idle room state.
    setenv("MP_PIPELINE_LAZY","1",0);
    setenv("MP_PIPELINE_EVICT","3600",0);
    // Sample 1/64 GX draws to expose the real decoder costs, including native
    // device/pipeline/bind-group work. No clocks on unsampled draws; aggregates
    // are emitted after the existing decoder drain. Set 0 for an overhead A/B.
    setenv("MP_GX_DRAW_PREP_PROFILE","1",0);
    // The 991 MiB usable default is exhausted by measured real game resources.
    // Reserve 2 GiB from the console's reported 4.5 GiB direct-memory pool;
    // retain the driver's coherent arena and normal allocation/fence semantics.
    setenv("ORBIS_ARENA_MIB","2048",0);
    // Native suspend/Close Application waits for a frame-boundary SubmitDone
    // handshake even when the GPU is idle (kernel stop reason 0xa0d0c00a).
    // Mesa's existing WSI path performs it once per presented frame, after
    // its completion wait; calling it for each Vulkan submission is incorrect.
    setenv("ORBIS_SUBMIT_DONE","1",1);
    cache_file_identity();
    if (prime_ps4_sqlite_init()!=0)
        std::fprintf(stderr,"orbis: could not configure SQLite native file identity\n");
    setenv("SDL_VIDEODRIVER","dummy",1);
    setenv("SDL_VIDEO_DRIVER","dummy",1);
    setenv("SDL_AUDIO_DRIVER","orbis",1);
    // SDL3 renamed this hint, and its environment object may already be cached
    // by a global initializer. Set the actual hints before Aurora initializes.
    if (!SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER,"dummy",SDL_HINT_OVERRIDE) ||
        !SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"orbis",SDL_HINT_OVERRIDE)) {
        message("Metroid Prime could not configure its native SDL services.");
        return 1;
    }
    const char *disc=getenv("MP_DISC");
    FILE *input=fopen(disc,"rb");
    if (!input) {
        message("Metroid Prime game data is missing. Prepare your supported game dump on the PC, then copy MetroidPrime.iso to /data/project-prime/data/ and launch again.");
        return 2;
    }
    NodDiscStream stream{input,read_at,length,close_stream};
    NodHandle *check=nullptr;
    NodResult result=nod_disc_open_stream(&stream,nullptr,&check);
    if (result!=NOD_RESULT_OK) {
        std::fprintf(stderr,"Disc preflight failed: %s\n",nod_error_message());
        message("Metroid Prime could not read the game data. Use a complete, supported GameCube ISO prepared with the PC import tool. Check the import receipt and copy the image again.");
        return 3;
    }
    nod_free(check);
    std::fprintf(stdout,"Project Prime: native Orbis entry; ISO preflight passed; entering real engine\n");
    char executable[]="/app0/eboot.bin";
    char *fallback_argv[]={executable,nullptr};
    if (argc<1 || !argv || !argv[0]) { argc=1;argv=fallback_argv; }
    if(candidate.remastered && (!getenv("MP_PS4_ASSETS_READY") || std::strcmp(getenv("MP_PS4_ASSETS_READY"),"1")))
        message("The installed Remastered assets are missing or incomplete. This launch will use original graphics. Reinstall the verified package to restore the Remastered assets; saves and settings are preserved.");
    prime_ps4_trace_start();
    std::fprintf(stdout,"orbis: candidate profile=%s; user/cache/mod paths isolated; baseline saves read only\n",candidate.key.empty()?"legacy":candidate.key.c_str());
    std::fprintf(stdout,"orbis: command storage reuse=%s; change requires application restart\n",getenv("AURORA_COMMAND_STORAGE_REUSE")?getenv("AURORA_COMMAND_STORAGE_REUSE"):"1");
    const int status=aurora_main(argc,argv);
    prime_ps4_trace_stop();
    std::fprintf(stdout,"Project Prime: engine returned %d\n",status);
    std::fflush(stdout);std::fflush(stderr);
    if (status) {
        char failure[512];
        std::snprintf(failure,sizeof(failure),
            "Metroid Prime stopped during startup.\n\nCheck the game data and graphics report.\n\nDiagnostic log: %s",boot_log_location());
        message(failure);
    }
    return status;
}

int main(int argc,char **argv) {
    const int status=run_game(argc,argv);
    std::fflush(stdout);std::fflush(stderr);
    prime_ps4_exit_to_shell(status);
}
