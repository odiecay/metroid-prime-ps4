// SPDX-License-Identifier: MIT
// Native logging replaces the desktop terminal/pipe/fork capture service.
#include "port_log_file.h"
#include <cerrno>
#include <cstdlib>
#include <unistd.h>

extern "C" int prime_ps4_log_open(const char *);
extern "C" int prime_ps4_log_active(void);

namespace PortLogFile {
std::string Path() { const char* path=std::getenv("MP_PS4_BOOT_LOG"); return path?path:"/data/project-prime/logs/boot.log"; }
std::string SharedPath() { return {}; }
bool Start() { return prime_ps4_log_open(Path().c_str())==0; }
bool Active() { return prime_ps4_log_active()!=0; }
// As on desktop, the caller already prints the line to stdout/stderr. Native
// _write/_writev routing captures it directly; a second copy would duplicate it.
void Write(const char *,const char *) {}
void WriteRaw(const char *data,size_t size) {
    const int saved=errno;
    size_t done=0;
    while(done<size) {
        const auto n=write(STDERR_FILENO,data+done,size-done);
        if(n>0) done+=size_t(n);
        else if(n<0 && errno==EINTR) continue;
        else break;
    }
    errno=saved;
}
}
