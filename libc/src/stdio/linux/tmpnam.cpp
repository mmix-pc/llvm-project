//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/tmpnam.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <linux/stat.h>

namespace LIBC_NAMESPACE_DECL {
namespace {
cpp::Atomic<unsigned long long> sequence{0};
char internal_buffer[L_tmpnam];
} // namespace

LLVM_LIBC_FUNCTION(char *, tmpnam, (char *buffer)) {
  char candidate[L_tmpnam] = "/tmp/libc-";
  constexpr size_t PREFIX = sizeof("/tmp/libc-") - 1;
  static_assert(L_tmpnam >= PREFIX + 32 + 1);
  constexpr char HEX[] = "0123456789abcdef";
  for (int attempt = 0; attempt < TMP_MAX; ++attempt) {
    unsigned char bytes[16];
    size_t filled = 0;
    while (filled < 8) {
      auto result = linux_syscalls::getrandom(bytes + filled, 8 - filled, 0);
      if (!result.has_value()) {
        if (result.error() == EINTR)
          continue;
        libc_errno = result.error();
        return nullptr;
      }
      if (result.value() == 0) {
        libc_errno = EIO;
        return nullptr;
      }
      filled += static_cast<size_t>(result.value());
    }
    // Distinguish calls even if the random source repeats its output.
    auto serial = sequence.fetch_add(1, cpp::MemoryOrder::RELAXED);
    for (size_t i = 0; i < 8; ++i)
      bytes[8 + i] = static_cast<unsigned char>(serial >> (i * 8));
    for (size_t i = 0; i < 16; ++i) {
      candidate[PREFIX + i * 2] = HEX[bytes[i] >> 4];
      candidate[PREFIX + i * 2 + 1] = HEX[bytes[i] & 15];
    }
    candidate[PREFIX + 32] = '\0';
    struct statx info;
    auto result = linux_syscalls::statx(AT_FDCWD, candidate,
                                        AT_SYMLINK_NOFOLLOW, STATX_TYPE, &info);
    if (result.has_value())
      continue;
    if (result.error() != ENOENT) {
      libc_errno = result.error();
      return nullptr;
    }
    // This only names an absent path; another process can create it before
    // the caller opens it. Use tmpfile or mkstemp for atomic creation.
    char *output = buffer ? buffer : internal_buffer;
    for (size_t i = 0; i <= PREFIX + 32; ++i)
      output[i] = candidate[i];
    return output;
  }
  libc_errno = EEXIST;
  return nullptr;
}
} // namespace LIBC_NAMESPACE_DECL
