#include "prime_dawn_call_profile.h"
static thread_local volatile unsigned consumed=0;
static void consume(unsigned n) noexcept {consumed=consumed+n;}
extern "C" void external_profile_call() {
    prime::dawn_call_profile::call(prime::dawn_call_profile::Category::Descriptor,consume,1u);
}
