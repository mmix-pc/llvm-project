//===-- POSIX and XSI limits -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_POSIX_LIMITS_MACROS_H
#define LLVM_LIBC_MACROS_POSIX_LIMITS_MACROS_H

// Standard minimum limits are not feature-availability declarations.
#define _POSIX_AIO_LISTIO_MAX 2
#define _POSIX_AIO_MAX 1
#define _POSIX_CHILD_MAX 25
#define _POSIX_CLOCKRES_MIN 20000000
#define _POSIX_DELAYTIMER_MAX 32
#define _POSIX_LINK_MAX 8
#define _POSIX_LOGIN_NAME_MAX 9
#define _POSIX_MQ_OPEN_MAX 8
#define _POSIX_MQ_PRIO_MAX 32
#define _POSIX_NGROUPS_MAX 8
#define _POSIX_OPEN_MAX 20
#define _POSIX_PIPE_BUF 512
#define _POSIX_RE_DUP_MAX 255
#define _POSIX_RTSIG_MAX 8
#define _POSIX_SEM_NSEMS_MAX 256
#define _POSIX_SEM_VALUE_MAX 32767
#define _POSIX_SIGQUEUE_MAX 32
#define _POSIX_SSIZE_MAX 32767
#define _POSIX_SS_REPL_MAX 4
#define _POSIX_STREAM_MAX 8
#define _POSIX_SYMLINK_MAX 255
#define _POSIX_SYMLOOP_MAX 8
#define _POSIX_THREAD_KEYS_MAX 128
#define _POSIX_THREAD_THREADS_MAX 64
#define _POSIX_TIMER_MAX 32
#define _POSIX_TTY_NAME_MAX 9
#define _POSIX_TZNAME_MAX 6

#define _POSIX2_BC_BASE_MAX 99
#define _POSIX2_BC_DIM_MAX 2048
#define _POSIX2_BC_SCALE_MAX 99
#define _POSIX2_BC_STRING_MAX 1000
#define _POSIX2_CHARCLASS_NAME_MAX 14
#define _POSIX2_COLL_WEIGHTS_MAX 2
#define _POSIX2_EXPR_NEST_MAX 32
#define _POSIX2_LINE_MAX 2048
#define _POSIX2_RE_DUP_MAX 255

#define _XOPEN_IOV_MAX 16
#define _XOPEN_NAME_MAX 255
#define _XOPEN_PATH_MAX 1024

// Portable utility bounds; providing these does not provide the utilities.
#ifndef BC_BASE_MAX
#define BC_BASE_MAX _POSIX2_BC_BASE_MAX
#endif
#ifndef BC_DIM_MAX
#define BC_DIM_MAX _POSIX2_BC_DIM_MAX
#endif
#ifndef BC_SCALE_MAX
#define BC_SCALE_MAX _POSIX2_BC_SCALE_MAX
#endif
#ifndef BC_STRING_MAX
#define BC_STRING_MAX _POSIX2_BC_STRING_MAX
#endif
#ifndef CHARCLASS_NAME_MAX
#define CHARCLASS_NAME_MAX _POSIX2_CHARCLASS_NAME_MAX
#endif
#ifndef COLL_WEIGHTS_MAX
#define COLL_WEIGHTS_MAX _POSIX2_COLL_WEIGHTS_MAX
#endif
#ifndef EXPR_NEST_MAX
#define EXPR_NEST_MAX _POSIX2_EXPR_NEST_MAX
#endif
#ifndef LINE_MAX
#define LINE_MAX _POSIX2_LINE_MAX
#endif

// The BSD regex engine uses _POSIX2_RE_DUP_MAX as its repetition bound.
#ifndef RE_DUP_MAX
#define RE_DUP_MAX _POSIX2_RE_DUP_MAX
#endif
#ifndef NGROUPS_MAX
#ifdef __linux__
// Linux UAPI include/uapi/linux/limits.h.
#define NGROUPS_MAX 65536
#else
#define NGROUPS_MAX _POSIX_NGROUPS_MAX
#endif
#endif
#ifndef LONG_BIT
#define LONG_BIT (__SIZEOF_LONG__ * __CHAR_BIT__)
#endif
#ifndef WORD_BIT
#define WORD_BIT (__SIZEOF_INT__ * __CHAR_BIT__)
#endif

// Catalog identifier/text representation limits do not advertise catalog or
// named-locale support. Formatting argument indices have no smaller fixed cap.
#ifndef NL_ARGMAX
#define NL_ARGMAX 4096
#endif
#ifndef NL_LANGMAX
#define NL_LANGMAX _POSIX2_LINE_MAX
#endif
#ifndef NL_MSGMAX
#define NL_MSGMAX __INT_MAX__
#endif
#ifndef NL_SETMAX
#define NL_SETMAX __INT_MAX__
#endif
#ifndef NL_TEXTMAX
#define NL_TEXTMAX __INT_MAX__
#endif

#endif // LLVM_LIBC_MACROS_POSIX_LIMITS_MACROS_H
