//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#define SYSLOG_NAMES
#include "include/llvm-libc-macros/syslog-macros.h"
#include "include/llvm-libc-macros/syslog-macros.h"

#ifdef SYSLOG_NAMES_CONST
#define NAME_TYPE const char *
#define ENTRY_TYPE const CODE *
#else
#define NAME_TYPE char *
#define ENTRY_TYPE CODE *
#endif

#ifdef __cplusplus
static_assert(__is_same(decltype(LOG_MASK((int)0)), int));
static_assert(__is_same(decltype(LOG_UPTO((int)0)), int));
static_assert(__is_same(decltype(prioritynames[0].c_name), NAME_TYPE));
static_assert(__is_same(decltype(&prioritynames[0]), ENTRY_TYPE));
#else
_Static_assert(_Generic(LOG_MASK((int)0), int: 1, default: 0), "mask type");
_Static_assert(_Generic(LOG_UPTO((int)0), int: 1, default: 0), "upto type");
_Static_assert(__builtin_types_compatible_p(__typeof__(prioritynames[0].c_name),
                                          NAME_TYPE), "name type");
_Static_assert(__builtin_types_compatible_p(__typeof__(&prioritynames[0]),
                                          ENTRY_TYPE), "table type");
#endif

static int check_syslog_names(void) {
  for (int priority = LOG_EMERG; priority <= LOG_DEBUG; ++priority) {
    int argument = priority;
    if (LOG_MASK(argument++) != (1 << priority) || argument != priority + 1)
      return 4;
    argument = priority;
    if (LOG_UPTO(argument++) != (1 << (priority + 1)) - 1 ||
        argument != priority + 1)
      return 5;
  }
  if (prioritynames[12].c_name || prioritynames[12].c_val != -1 ||
      facilitynames[22].c_name || facilitynames[22].c_val != -1)
    return 1;
  if (prioritynames[0].c_val != LOG_ALERT ||
      facilitynames[11].c_val != LOG_SYSLOG)
    return 2;
#ifndef SYSLOG_NAMES_CONST
  prioritynames[0].c_val = LOG_DEBUG;
  if (prioritynames[0].c_val != LOG_DEBUG)
    return 3;
#endif
  return 0;
}
