#ifndef _LIBISOFS_CONFIG_H
#define _LIBISOFS_CONFIG_H

#ifdef _WIN32

/* NOTE: _FILE_OFFSET_BITS=64 must be defined on the compiler command line
   (-D_FILE_OFFSET_BITS=64), NOT here. Defining it in config.h means some
   translation units see it after <sys/types.h> and get 32-bit off_t while
   others get 64-bit — struct layout mismatch → crashes in Joliet writer. */
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <sys/types.h>
#include <errno.h>

#include "winpatch/_patch.h"
#include "winpatch/types.h"
#include "winpatch/pwd.h"
#include "winpatch/grp.h"
#include "winpatch/langinfo.h"
#include "winpatch/fnmatch.h"
#include "winpatch/lstat.h"
#include "winpatch/unistd.h"

#define _POSIX_C_SOURCE 200809L
#define Libburnia_timezonE timezone

#undef HAVE_ZLIB
#define HAVE_ICONV 1
#undef HAVE_TIMEGM

#else /* Linux */

#undef HAVE_ZLIB
#define HAVE_ICONV 1
#define HAVE_PTHREAD 1
#define HAVE_TIMEGM 1

#endif /* _WIN32 */

#undef HAVE_LIBACL
#undef HAVE_XATTR
#define HAVE_STDINT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1

#endif /* _LIBISOFS_CONFIG_H */
