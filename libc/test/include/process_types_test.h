//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-types/idtype_t.h"
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

int main(void) {
  idtype_t selector = P_PID;
  return selector != P_PID;
}
