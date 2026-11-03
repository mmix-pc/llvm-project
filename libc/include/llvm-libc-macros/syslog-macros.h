//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Definition of macros from syslog.h.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_SYSLOG_MACROS_H
#define LLVM_LIBC_MACROS_SYSLOG_MACROS_H

// Options for openlog
#define LOG_PID 0x01
#define LOG_CONS 0x02
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08
#define LOG_NOWAIT 0x10
#define LOG_PERROR 0x20

#define LOG_MASK(priority) (1U << (priority))
#define LOG_UPTO(priority) ((1U << ((priority) + 1)) - 1)

// Facilities for openlog
#define LOG_KERN (0 << 3)
#define LOG_USER (1 << 3)
#define LOG_MAIL (2 << 3)
#define LOG_DAEMON (3 << 3)
#define LOG_AUTH (4 << 3)
#define LOG_LPR (6 << 3)
#define LOG_NEWS (7 << 3)
#define LOG_UUCP (8 << 3)
#define LOG_CRON (9 << 3)
#define LOG_LOCAL0 (16 << 3)
#define LOG_LOCAL1 (17 << 3)
#define LOG_LOCAL2 (18 << 3)
#define LOG_LOCAL3 (19 << 3)
#define LOG_LOCAL4 (20 << 3)
#define LOG_LOCAL5 (21 << 3)
#define LOG_LOCAL6 (22 << 3)
#define LOG_LOCAL7 (23 << 3)

// Priorities for syslog
#define LOG_EMERG 0
#define LOG_ALERT 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_DEFAULT)
// BSD/GNU helpers use an encoded facility, except LOG_FAC's numeric result.
#define LOG_PRIMASK 0x07
#define LOG_FACMASK 0x03f8
#define LOG_PRI(priority) ((priority) & LOG_PRIMASK)
#define LOG_FAC(priority) (((priority) & LOG_FACMASK) >> 3)
#define LOG_MAKEPRI(facility, priority) ((facility) | (priority))
#define LOG_NFACILITIES 24
#define LOG_SYSLOG (5 << 3)
#define LOG_AUTHPRIV (10 << 3)
#define LOG_FTP (11 << 3)

#ifdef SYSLOG_NAMES
#define INTERNAL_NOPRI 0x10
#define INTERNAL_MARK (LOG_NFACILITIES << 3)

#ifdef SYSLOG_NAMES_CONST
#define __LLVM_LIBC_SYSLOG_CONST const
#define __LLVM_LIBC_SYSLOG_NAME(name) name
#else
#define __LLVM_LIBC_SYSLOG_CONST
// The historical CODE member is char *, but the strings are not writable.
#ifdef __cplusplus
#define __LLVM_LIBC_SYSLOG_NAME(name) const_cast<char *>(name)
#else
#define __LLVM_LIBC_SYSLOG_NAME(name) ((char[]){name})
#endif
#endif

typedef struct _code {
  __LLVM_LIBC_SYSLOG_CONST char *c_name;
  int c_val;
} CODE;

// SYSLOG_NAMES requests definitions in one translation unit, not libc exports.
__LLVM_LIBC_SYSLOG_CONST CODE prioritynames[] = {
    {__LLVM_LIBC_SYSLOG_NAME("alert"), LOG_ALERT},
    {__LLVM_LIBC_SYSLOG_NAME("crit"), LOG_CRIT},
    {__LLVM_LIBC_SYSLOG_NAME("debug"), LOG_DEBUG},
    {__LLVM_LIBC_SYSLOG_NAME("emerg"), LOG_EMERG},
    {__LLVM_LIBC_SYSLOG_NAME("err"), LOG_ERR},
    {__LLVM_LIBC_SYSLOG_NAME("error"), LOG_ERR},
    {__LLVM_LIBC_SYSLOG_NAME("info"), LOG_INFO},
    {__LLVM_LIBC_SYSLOG_NAME("none"), INTERNAL_NOPRI},
    {__LLVM_LIBC_SYSLOG_NAME("notice"), LOG_NOTICE},
    {__LLVM_LIBC_SYSLOG_NAME("panic"), LOG_EMERG},
    {__LLVM_LIBC_SYSLOG_NAME("warn"), LOG_WARNING},
    {__LLVM_LIBC_SYSLOG_NAME("warning"), LOG_WARNING},
    {0, -1}};

__LLVM_LIBC_SYSLOG_CONST CODE facilitynames[] = {
    {__LLVM_LIBC_SYSLOG_NAME("auth"), LOG_AUTH},
    {__LLVM_LIBC_SYSLOG_NAME("authpriv"), LOG_AUTHPRIV},
    {__LLVM_LIBC_SYSLOG_NAME("cron"), LOG_CRON},
    {__LLVM_LIBC_SYSLOG_NAME("daemon"), LOG_DAEMON},
    {__LLVM_LIBC_SYSLOG_NAME("ftp"), LOG_FTP},
    {__LLVM_LIBC_SYSLOG_NAME("kern"), LOG_KERN},
    {__LLVM_LIBC_SYSLOG_NAME("lpr"), LOG_LPR},
    {__LLVM_LIBC_SYSLOG_NAME("mail"), LOG_MAIL},
    {__LLVM_LIBC_SYSLOG_NAME("mark"), INTERNAL_MARK},
    {__LLVM_LIBC_SYSLOG_NAME("news"), LOG_NEWS},
    {__LLVM_LIBC_SYSLOG_NAME("security"), LOG_AUTH},
    {__LLVM_LIBC_SYSLOG_NAME("syslog"), LOG_SYSLOG},
    {__LLVM_LIBC_SYSLOG_NAME("user"), LOG_USER},
    {__LLVM_LIBC_SYSLOG_NAME("uucp"), LOG_UUCP},
    {__LLVM_LIBC_SYSLOG_NAME("local0"), LOG_LOCAL0},
    {__LLVM_LIBC_SYSLOG_NAME("local1"), LOG_LOCAL1},
    {__LLVM_LIBC_SYSLOG_NAME("local2"), LOG_LOCAL2},
    {__LLVM_LIBC_SYSLOG_NAME("local3"), LOG_LOCAL3},
    {__LLVM_LIBC_SYSLOG_NAME("local4"), LOG_LOCAL4},
    {__LLVM_LIBC_SYSLOG_NAME("local5"), LOG_LOCAL5},
    {__LLVM_LIBC_SYSLOG_NAME("local6"), LOG_LOCAL6},
    {__LLVM_LIBC_SYSLOG_NAME("local7"), LOG_LOCAL7},
    {0, -1}};

#undef __LLVM_LIBC_SYSLOG_NAME
#undef __LLVM_LIBC_SYSLOG_CONST
#endif // SYSLOG_NAMES
#endif // BSD/GNU namespace

#endif // LLVM_LIBC_MACROS_SYSLOG_MACROS_H
