#ifndef _WINPATCH_UNISTD_H
#define _WINPATCH_UNISTD_H

#include <errno.h>
#include <windows.h>
#include <string.h>
#include <sys/types.h>

static inline ssize_t readlink(const char *path, char *buf, size_t bufsize) {
    (void)path;
    (void)buf;
    (void)bufsize;

	// Always fail since Windows rarely has real symlinks	    
    errno = EINVAL;  // or ENOENT
    return -1;
}

#endif /* _WINPATCH_UNISTD_H */
