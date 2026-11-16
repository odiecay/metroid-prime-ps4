#define PRIME_LIFECYCLE_TEST 1
#include "../platform/ps4/lifecycle.cpp"
#include <cassert>
#include <climits>
#include <fstream>
#include <iterator>

extern "C" unsigned long sceKernelGetProcessTime() { return 100; }
extern "C" int sceKernelDebugOutText(int,const char*) { return 0; }
extern "C" int sceSysmoduleLoadModuleInternal(unsigned) { return 0; }
extern "C" int sceSystemServiceLoadExec(const char*,const char*[]) { assert(false);return -1; }
extern "C" int sceKernelUsleep(unsigned) { assert(false);return 0; }

int main() {
    prime_ps4_lifecycle_install();
    struct sigaction action{};
    assert(sigaction(15,nullptr,&action)==0 && (action.sa_flags&SA_SIGINFO));
    assert(!prime_ps4_quit_requested());
    // A monitor may start with a blocked native signal even when main is
    // unblocked. Exercise the actual production preparation and handler.
    std::thread worker([] {
        sigset_t blocked{};sigaddset(&blocked,15);
        assert(pthread_sigmask(SIG_BLOCK,&blocked,nullptr)==0);
        assert(raise(15)==0 && !prime_ps4_quit_requested());
        prepare_close_thread("test-blocked-monitor");
        assert(prime_ps4_quit_requested());
        sigset_t current{};
        assert(pthread_sigmask(0,nullptr,&current)==0 && sigismember(&current,15)==0);
    });worker.join();
    const int validFd=logFd;
    logFd=INT_MAX;
    errno=EDOM;
    assert(raise(15)==0);
    assert(errno==EDOM); // Marker failure must not alter interrupted code.
    logFd=validFd;
    // Late library changes must be observable rather than assumed absent.
    const auto installed=close_signal_state();
    struct sigaction ignored{};ignored.sa_handler=SIG_IGN;
    assert(sigaction(15,&ignored,nullptr)==0);
    const auto replaced=close_signal_state();
    assert(!(installed==replaced) && replaced.handler==uintptr_t(SIG_IGN));
    record_close_state("test-late-disposition-change",0,replaced);
    assert(sigaction(15,&action,nullptr)==0);
    std::ifstream file(PRIME_SHUTDOWN_LOG);
    const std::string log((std::istreambuf_iterator<char>(file)),{});
    assert(log.find("role=main")!=std::string::npos);
    assert(log.find("role=test-blocked-monitor")!=std::string::npos);
    assert(log.find("handler=native-close")!=std::string::npos);
    assert(log.find("received close signal=15 code=")!=std::string::npos);
    assert(log.find(" sender=")!=std::string::npos);
    assert(log.find("role=test-late-disposition-change")!=std::string::npos);
    assert(log.find("handler=other")!=std::string::npos);
}
