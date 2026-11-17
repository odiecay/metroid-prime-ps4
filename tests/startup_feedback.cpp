// SPDX-License-Identifier: MIT
// Compile the actual nonrender startup logger. No graphics, input or game
// implementation is linked; adding such a dependency makes this test fail.
#include "port_startup.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace {
uint64_t now=1000000000;
unsigned clocks=0;
FILE* capture=nullptr;
std::string log_text() {
  std::fflush(stderr);
  const auto size=lseek(fileno(capture),0,SEEK_END); assert(size>=0);
  std::string text(size,'\0');
  assert(pread(fileno(capture),text.data(),text.size(),0)==size);
  return text;
}
}
extern "C" uint64_t SDL_GetTicksNS() { ++clocks; return now; }

int main() {
  capture=std::tmpfile(); assert(capture);
  const int saved=dup(fileno(stderr)); assert(saved>=0);
  assert(dup2(fileno(capture),fileno(stderr))>=0);
  PortStartup::Stage("Not running"); assert(clocks==0 && log_text().empty());
  {
    PortStartup::Scope scope; assert(clocks==1);
    now+=250000000;
    PortStartup::Stage("Graphics initialized");
    assert(log_text().find("stage 250.0 ms, total 250.0 ms")!=std::string::npos);
    const auto previous=log_text(); const auto reads=clocks;
    PortStartup::Stage("Graphics initialized"); PortStartup::Stage(nullptr);
    assert(log_text()==previous && clocks==reads);
    std::string temporary="Temporary owned phase";
    PortStartup::Stage(temporary.c_str()); temporary.assign(4096,'x');
    PortStartup::Stage("Checking game data");
    assert(log_text().find("Temporary owned phase -> Checking game data")!=std::string::npos);
    PortStartup::Stage(std::string(300,'x').c_str());
    now+=125000000;
    PortStartup::Stage("Game initialization complete");
    assert(log_text().find(std::string(95,'x')+" -> Game initialization complete")!=std::string::npos);
    assert(log_text().find("stage 125.0 ms, total 375.0 ms")!=std::string::npos);
  }
  const auto finished=log_text(); const auto reads=clocks;
  PortStartup::Stage("After exit"); assert(log_text()==finished && clocks==reads);
  {
    now+=1000000000; PortStartup::Scope restart;
    now+=1000000; PortStartup::Stage("Restart ready");
    assert(log_text().find("stage 1.0 ms, total 1.0 ms")!=std::string::npos);
  }
  std::fflush(stderr); assert(dup2(saved,fileno(stderr))>=0); close(saved); std::fclose(capture);
  std::puts("startup_logging_only monotonic_elapsed=1 owned_bounded_label=1 no_duplicate_logs=1 restart_scope=1 no_renderer_linked=1");
}
