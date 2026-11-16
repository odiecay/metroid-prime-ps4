// SPDX-License-Identifier: MIT
// Private to native SQLite and its diagnostic. Do not change the shared SDK.
// The bundled fcntl commands/open flags are BSD, but its flock layout and lock
// type constants are still Linux. ABI: FreeBSD releng/9.0 sys/sys/fcntl.h.
#pragma once
#ifndef __ORBIS__
#error Native Orbis SQLite ABI header used with another target
#endif
#include <stddef.h>
#define flock PrimeSdkLegacyFlock
#include <fcntl.h>
#undef flock
struct flock {
    off_t l_start,l_len;
    pid_t l_pid;
    short l_type,l_whence;
    int l_sysid;
};
#undef F_RDLCK
#undef F_WRLCK
#undef F_UNLCK
#define F_RDLCK 1
#define F_WRLCK 3
#define F_UNLCK 2
// Avoid the SDK's Linux-only command 1030. SQLite uses F_DUPFD + F_SETFD.
#undef F_DUPFD_CLOEXEC
#ifdef __cplusplus
static_assert(sizeof(struct flock)==32 && offsetof(struct flock,l_type)==20);
#else
_Static_assert(sizeof(struct flock)==32 && offsetof(struct flock,l_type)==20,"Native flock ABI");
#endif
