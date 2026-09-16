#pragma once
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/* Vendor libc reads DHCP properties through this inherited read-only mapping.
 * The backing file is unlinked by init, so closing its descriptor loses access
 * for the next exec, even though ANDROID_PROPERTY_WORKSPACE remains set. */
static inline int tc002PropertyWorkspaceFd(void) {
    const char* value = getenv("ANDROID_PROPERTY_WORKSPACE");
    int fd = -1;
    unsigned long bytes = 0;
    char extra;
    struct stat info;
    if (!value || sscanf(value, "%d,%lu%c", &fd, &bytes, &extra) != 2 ||
        fd < 3 || fd >= 1024 || bytes == 0 || fstat(fd, &info) != 0 ||
        !S_ISREG(info.st_mode) || info.st_size < 0 ||
        (unsigned long)info.st_size < bytes ||
        (fcntl(fd, F_GETFL) & O_ACCMODE) != O_RDONLY) return -1;
    return fd;
}

static inline void tc002PrepareExecDescriptors(void) {
    const int properties = tc002PropertyWorkspaceFd();
    for (int fd = 3; fd < 1024; ++fd)
        fcntl(fd, F_SETFD, fd == properties ? 0 : FD_CLOEXEC);
}
