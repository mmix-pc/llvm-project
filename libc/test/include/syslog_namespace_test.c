// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#define __LIBC_EXPLICIT_NAMESPACE 1
#define __LIBC_USE_POSIX2024 1
#define SYSLOG_NAMES
#include "include/llvm-libc-macros/syslog-macros.h"

#if defined(LOG_SYSLOG) || defined(LOG_PRI) || defined(INTERNAL_NOPRI)
#error "BSD/GNU extensions leaked into the explicit POSIX namespace"
#endif

// These names must remain available when the extension is not selected.
int CODE, prioritynames, facilitynames;
int main(void) { return CODE + prioritynames + facilitynames; }
