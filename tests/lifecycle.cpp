#include "lifecycle.h"
#include "shutdown_policy.hpp"
#include <cassert>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
namespace {struct ExitWait {}; int calls; int exitResult;}
extern "C" unsigned long sceKernelGetProcessTime() { return 100; }
extern "C" int sceKernelDebugOutText(int,const char*) { return 0; }
extern "C" int sceSysmoduleLoadModuleInternal(unsigned) { return 0; }
extern "C" int sceSystemServiceLoadExec(const char* path,const char* args[]) {
  assert(!std::strcmp(path,"exit") && args==nullptr);++calls;return exitResult;
}
extern "C" int sceKernelUsleep(unsigned micros) {
  assert(micros==100000 && calls==1);throw ExitWait{};
}
int main(int argc,char** argv) {
  assert(argc==3);
  prime::ShutdownDeadline deadline;
  assert(!deadline.expired(9000000));
  deadline.begin(100);deadline.begin(7000000);
  assert(!deadline.expired(8000099) && deadline.expired(8000100) && !deadline.expired(99));
  sigset_t initiallyBlocked{};
  sigemptyset(&initiallyBlocked);sigaddset(&initiallyBlocked,15);
  assert(pthread_sigmask(SIG_BLOCK,&initiallyBlocked,nullptr)==0);
  {std::ofstream old(argv[2]);old<<"previous close evidence";}
  prime_ps4_lifecycle_install();
  {
    std::ifstream previous(std::string(argv[2])+".previous");std::string text;
    std::getline(previous,text);assert(text=="previous close evidence");
  }
  assert(!prime_ps4_quit_requested());
  struct sigaction installed{};
  assert(sigaction(15,nullptr,&installed)==0 && (installed.sa_flags&SA_RESTART));
  sigset_t mask{};
  assert(pthread_sigmask(SIG_SETMASK,nullptr,&mask)==0 && !sigismember(&mask,15));
  std::raise(15);
  assert(prime_ps4_quit_requested() && calls==0); // signal handler only requests shutdown
  prime_ps4_shutdown_begin("test saving");
  prime_ps4_shutdown_stage("test cleanup complete");
  exitResult=std::strcmp(argv[1],"refused")==0?-1:0;
  try { prime_ps4_exit_to_shell(0);assert(false); }
  catch(const ExitWait&) {}
  assert(calls==1);
  std::ifstream in(argv[2]);
  std::string log((std::istreambuf_iterator<char>(in)),{});
  assert(log.find("test cleanup complete")<log.find("requesting shell exit"));
  assert(log.find("received close signal=15")!=std::string::npos);
  assert(log.find("shell exit request returned=")!=std::string::npos);
}
