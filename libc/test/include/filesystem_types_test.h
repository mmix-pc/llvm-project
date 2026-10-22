//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-types/struct_FTW.h"
#include "include/llvm-libc-types/struct_f_owner_ex.h"
#include "include/llvm-libc-macros/linux/fcntl-macros.h"

#ifdef __cplusplus
#define CHECK static_assert
#define SAME_TYPE(a, b) __is_same(decltype(a), decltype(b))
#else
#define CHECK _Static_assert
#define SAME_TYPE(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#endif

#if !defined(F_OWNER_PID) || !defined(F_OWNER_PGRP)
#error "owner kinds must be macros"
#endif
CHECK(SAME_TYPE(((struct f_owner_ex *)0)->type, (int)0), "owner discriminator");
CHECK(SAME_TYPE(((struct f_owner_ex *)0)->pid, (pid_t)0), "owner ID");
CHECK(F_OWNER_PID == 1 && F_OWNER_PGRP == 2, "Linux owner ABI");
CHECK(SAME_TYPE(((struct FTW *)0)->base, (int)0), "pathname offset");
CHECK(SAME_TYPE(((struct FTW *)0)->level, (int)0), "tree depth");
CHECK(__builtin_offsetof(struct FTW, level) == sizeof(int), "FTW order");
CHECK(POSIX_FADV_NORMAL == 0 && POSIX_FADV_RANDOM == 1 &&
      POSIX_FADV_SEQUENTIAL == 2 && POSIX_FADV_WILLNEED == 3,
      "Linux fadvise ABI");
#ifdef __s390x__
CHECK(POSIX_FADV_DONTNEED == 6 && POSIX_FADV_NOREUSE == 7, "s390x ABI");
#else
CHECK(POSIX_FADV_DONTNEED == 4 && POSIX_FADV_NOREUSE == 5, "generic Linux ABI");
#endif

int main(void) { return 0; }
