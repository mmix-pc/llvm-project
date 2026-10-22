//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-types/idtype_t.h"
#include "include/llvm-libc-types/posix_spawnattr_t.h"
#include "include/llvm-libc-macros/spawn-macros.h"
#include "include/llvm-libc-macros/linux/sys-resource-macros.h"

#ifdef __cplusplus
#define CHECK static_assert
CHECK(__is_enum(idtype_t), "wait selector is an enumeration");
#else
#define CHECK _Static_assert
#endif

CHECK(P_ALL == 0 && P_PID == 1 && P_PGID == 2, "Linux wait selector ABI");
CHECK(RLIM_SAVED_MAX == RLIM_INFINITY && RLIM_SAVED_CUR == RLIM_INFINITY,
      "Linux can represent every resource limit");
CHECK(sizeof(((posix_spawnattr_t *)0)->__sigdefault) == sizeof(sigset_t),
      "spawn default signals use the public signal set");
CHECK(sizeof(((posix_spawnattr_t *)0)->__sigmask) == sizeof(sigset_t),
      "spawn mask uses the public signal set");
CHECK((POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF |
       POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSCHEDPARAM |
       POSIX_SPAWN_SETSCHEDULER | POSIX_SPAWN_SETSID) == 0xbf,
      "spawn flags occupy independent bits");

int main(void) {
  idtype_t selector = P_PID;
  posix_spawnattr_t attributes;
  attributes.__flags = POSIX_SPAWN_RESETIDS;
  return selector != P_PID || attributes.__flags != POSIX_SPAWN_RESETIDS;
}
