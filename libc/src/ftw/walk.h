//===-- Private ftw/nftw adapter interface ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_FTW_WALK_H
#define LLVM_LIBC_SRC_FTW_WALK_H

#include "src/ftw/nftw.h"
#include <ftw.h>

namespace LIBC_NAMESPACE_DECL {
namespace ftw_internal {

// Older overlay headers lack Issue 8's flag. This is the value exported by
// this library's ftw.h, not admission of GNU FTW_ACTIONRETVAL semantics.
constexpr int XDEV = 16;
#ifdef FTW_XDEV
static_assert(FTW_XDEV == XDEV, "ftw.h must use the library's FTW_XDEV value");
#endif

struct Callback {
  int (*modern)(const char *, const struct stat *, int, struct FTW *);
  int (*legacy)(const char *, const struct stat *, int);

  int operator()(const char *path, const struct stat *status, int type,
                 struct FTW *position) const {
    if (modern)
      return modern(path, status, type, position);
    // Traditional ftw has no SLN event. Unlike nftw's permission-only NS,
    // its NS also reports a dangling link during the logical preorder walk.
    return legacy(path, status, type == FTW_SLN ? FTW_NS : type);
  }
};

int walk(const char *path, Callback callback, int fd_limit, int flags);

} // namespace ftw_internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_FTW_WALK_H
