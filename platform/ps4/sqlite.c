// SPDX-License-Identifier: MIT
// Native sceKernelStat reports zero inode/device on this application's storage,
// while fstat supplies the real identity. Keep SQLite's rename protection intact
// by querying the opened path. Other consumers keep their established stat ABI.
#include <sqlite3.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int path_identity(const char *path,struct stat *out) {
    const int fd=open(path,O_RDONLY);
    if(fd<0) return stat(path,out);
    const int result=fstat(fd,out);
    const int saved=errno;
    close(fd);
    errno=saved;
    return result;
}
int prime_ps4_sqlite_init(void) {
    sqlite3_vfs *vfs=sqlite3_vfs_find("unix");
    if(!vfs || vfs->iVersion<3 || !vfs->xSetSystemCall) return SQLITE_NOTFOUND;
    const int result=vfs->xSetSystemCall(vfs,"stat",(sqlite3_syscall_ptr)path_identity);
    fprintf(stdout,"orbis: SQLite native file identity adapter: %d\n",result);
    return result;
}
