//===-- Definition of macros from fcntl.h ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_LINUX_FCNTL_MACROS_H
#define LLVM_LIBC_MACROS_LINUX_FCNTL_MACROS_H

// File creation and file status flags.
#define O_APPEND 000002000
#define O_ASYNC 000020000
#define O_CLOEXEC 002000000
#define O_CREAT 000000100
#define O_DSYNC 000010000
#define O_EXCL 000000200
#define O_NOATIME 001000000
#define O_NOCTTY 000000400
#define O_NONBLOCK 000004000
#define O_NDELAY O_NONBLOCK
#define O_PATH 010000000
#define O_SYNC 004010000
#define O_RSYNC O_SYNC
#define O_FSYNC O_SYNC
#define O_TRUNC 000001000

#ifdef __aarch64__
#define O_DIRECT 000200000
#define O_DIRECTORY 000040000
#define O_NOFOLLOW 000100000
// Native AArch64 file offsets are always 64-bit.
#define O_LARGEFILE 0
#define O_TMPFILE 020040000
#else
#define O_DIRECT 000040000
#define O_DIRECTORY 000200000
#define O_NOFOLLOW 000400000
#define O_LARGEFILE 000100000
#define O_TMPFILE 020200000
#endif

// File access mode mask
#define O_ACCMODE 00000003

// File access mode flags
#define O_RDONLY 00000000
#define O_RDWR 00000002
#define O_WRONLY 00000001

// Special directory FD to indicate that the path argument to
// openat is relative to the current directory.
#define AT_FDCWD -100

// Special flag to the function unlinkat to indicate that it
// has to perform the equivalent of "rmdir" on the path argument.
#define AT_REMOVEDIR 0x200

// Special flag for functions like lstat to convey that symlinks
// should not be followed.
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_SYMLINK_FOLLOW 0x400

// Do not automount the terminal component during a metadata lookup.
#define AT_NO_AUTOMOUNT 0x800

// Allow empty relative pathname.
#define AT_EMPTY_PATH 0x1000

// Perform access checks using the effective user and group IDs.
#define AT_EACCESS 0x200

#define AT_STATX_SYNC_TYPE 0x6000
#define AT_STATX_SYNC_AS_STAT 0x0000
#define AT_STATX_FORCE_SYNC 0x2000
#define AT_STATX_DONT_SYNC 0x4000
#define AT_RECURSIVE 0x8000

// Values of SYS_fcntl commands.
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#define F_SETOWN 8
#define F_GETOWN 9
#define F_SETSIG 10
#define F_GETSIG 11
// Native 64-bit kernels (including x86-64's x32 ABI) use the ordinary
// commands for 64-bit locks. The separate commands belong to 32-bit kernels.
#if __SIZEOF_POINTER__ == 8 || defined(__x86_64__)
#define F_GETLK64 F_GETLK
#define F_SETLK64 F_SETLK
#define F_SETLKW64 F_SETLKW
#else
#define F_GETLK64 12
#define F_SETLK64 13
#define F_SETLKW64 14
#endif
#define F_SETOWN_EX 15
#define F_GETOWN_EX 16

// Open File Description Locks.
#define F_OFD_GETLK 36
#define F_OFD_SETLK 37
#define F_OFD_SETLKW 38

// Linux extension commands.
#define F_SETLEASE 1024
#define F_GETLEASE 1025
#define F_NOTIFY 1026
#define F_DUPFD_QUERY 1027
#define F_CREATED_QUERY 1028
#define F_DUPFD_CLOEXEC 1030
#define F_SETPIPE_SZ 1031
#define F_GETPIPE_SZ 1032
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034

#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#define F_SEAL_WRITE 0x0008
#define F_SEAL_FUTURE_WRITE 0x0010
#define F_SEAL_EXEC 0x0020

#define DN_ACCESS 0x00000001
#define DN_MODIFY 0x00000002
#define DN_CREATE 0x00000004
#define DN_DELETE 0x00000008
#define DN_RENAME 0x00000010
#define DN_ATTRIB 0x00000020
#define DN_MULTISHOT 0x80000000

// Close on execute for fcntl.
#define FD_CLOEXEC 1

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

#endif // LLVM_LIBC_MACROS_LINUX_FCNTL_MACROS_H
