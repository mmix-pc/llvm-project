//===-- Loader bootstrap I/O -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_IO_H
#define LLVM_LIBC_LOADER_LINUX_IO_H

#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

struct IOResult {
  size_t transferred;
  long error;
};

// Reads stop at EOF. Both operations preserve progress on a later error.
IOResult read_bytes(int fd, void *buffer, size_t size);
IOResult write_bytes(int fd, const void *buffer, size_t size);

class OwnedFd {
  int value = -1;

public:
  OwnedFd() = default;
  OwnedFd(const OwnedFd &) = delete;
  OwnedFd &operator=(const OwnedFd &) = delete;
  ~OwnedFd() { close(); }
  int get() const { return value; }
  long open_readonly(const char *path);
  // Consumes ownership even on failure; never retry Linux close after EINTR.
  long close();
};

// Fixed diagnostic, bounded write attempts, exit_group(127), then fatal trap.
[[noreturn]] void fatal_startup();

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_IO_H
