// SPDX-License-Identifier: MIT
#include "SDL_internal.h"
#include "filesystem/SDL_sysfilesystem.h"
#include <unistd.h>
#include <sys/stat.h>

char *SDL_SYS_GetBasePath(void) { return SDL_strdup("/app0/"); }
char *SDL_SYS_GetExeName(void) { return SDL_strdup("eboot.bin"); }
char *SDL_SYS_GetPrefPath(const char *org, const char *app) {
    (void)org; (void)app;
    if (mkdir("/data/project-prime", 0700) != 0 && access("/data/project-prime", W_OK) != 0) {
        SDL_SetError("Project Prime persistent storage is not writable");
        return NULL;
    }
    return SDL_strdup("/data/project-prime/");
}
char *SDL_SYS_GetUserFolder(SDL_Folder folder) {
    (void)folder;
    SDL_Unsupported();
    return NULL;
}
