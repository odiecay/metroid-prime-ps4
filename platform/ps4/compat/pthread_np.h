// SPDX-License-Identifier: MIT
#pragma once
#include_next <pthread_np.h>
#if defined(__ORBIS__)
#include <orbis/libkernel.h>
#ifdef __cplusplus
extern "C" {
#endif
// Abseil asks for FreeBSD's libc spelling; libkernel exposes the same native
// thread identity through this declared Orbis API.
static inline int pthread_getthreadid_np(void) { return scePthreadGetthreadid(); }
#ifdef __cplusplus
}
#endif
#endif
