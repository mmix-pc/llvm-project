//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <fcntl.h>

_Static_assert(SEEK_SET == 0 && SEEK_CUR == 1 && SEEK_END == 2,
               "fcntl.h supplies lock origins");
_Static_assert(S_IRWXU == (S_IRUSR | S_IWUSR | S_IXUSR), "owner permissions");
_Static_assert(S_IRWXG == (S_IRGRP | S_IWGRP | S_IXGRP), "group permissions");
_Static_assert(S_IRWXO == (S_IROTH | S_IWOTH | S_IXOTH), "other permissions");
_Static_assert(S_ISUID == 04000 && S_ISGID == 02000 && S_ISVTX == 01000,
               "special mode bits");
_Static_assert(O_RSYNC == O_SYNC && O_FSYNC == O_SYNC, "Linux sync aliases");
_Static_assert((O_LARGEFILE & O_DIRECTORY) == 0,
               "large files must not imply directories");
_Static_assert(AT_SYMLINK_FOLLOW == 0x400, "linkat flag");
_Static_assert(AT_STATX_SYNC_AS_STAT == 0 && AT_STATX_FORCE_SYNC == 0x2000 &&
                   AT_STATX_DONT_SYNC == 0x4000 && AT_STATX_SYNC_TYPE == 0x6000 &&
                   AT_RECURSIVE == 0x8000,
               "Linux metadata flags");

_Static_assert(F_GETLK == 5 && F_SETLK == 6 && F_SETLKW == 7,
               "feature macros must not rewrite ordinary commands");
#if __SIZEOF_POINTER__ == 8 || defined(__x86_64__)
_Static_assert(F_GETLK64 == 5 && F_SETLK64 == 6 && F_SETLKW64 == 7,
               "native 64-bit lock commands");
#else
_Static_assert(F_GETLK64 == 12 && F_SETLK64 == 13 && F_SETLKW64 == 14,
               "32-bit kernel large-file lock commands");
#endif

_Static_assert(F_SETLEASE == 1024 && F_GETLEASE == 1025 && F_NOTIFY == 1026 &&
                   F_DUPFD_QUERY == 1027 && F_CREATED_QUERY == 1028 &&
                   F_DUPFD_CLOEXEC == 1030 && F_SETPIPE_SZ == 1031 &&
                   F_GETPIPE_SZ == 1032 && F_ADD_SEALS == 1033 &&
                   F_GET_SEALS == 1034,
               "Linux extension commands");
_Static_assert(F_SEAL_SEAL == 1 && F_SEAL_SHRINK == 2 && F_SEAL_GROW == 4 &&
                   F_SEAL_WRITE == 8 && F_SEAL_FUTURE_WRITE == 16 &&
                   F_SEAL_EXEC == 32,
               "Linux seal flags");
_Static_assert(DN_ACCESS == 1 && DN_MODIFY == 2 && DN_CREATE == 4 &&
                   DN_DELETE == 8 && DN_RENAME == 16 && DN_ATTRIB == 32 &&
                   DN_MULTISHOT == 0x80000000,
               "Linux notification flags");
_Static_assert(FD_CLOEXEC == 1, "descriptor flag");

int main(void) { return 0; }
