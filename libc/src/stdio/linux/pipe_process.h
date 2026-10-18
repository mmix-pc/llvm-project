//===-- Pipe subprocess ownership ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_STDIO_LINUX_PIPE_PROCESS_H
#define LLVM_LIBC_SRC_STDIO_LINUX_PIPE_PROCESS_H
#include "hdr/types/pid_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
// The selected implementation controls cancellation, not process creation.
// Creation always goes through public posix_spawn and its admission policy.
class PipeProcess {
  [[maybe_unused]] int old_state = 0;
  [[maybe_unused]] int old_type = 0;
  [[maybe_unused]] bool restored = false;

public:
  pid_t pid = -1;
  PipeProcess();
  ~PipeProcess() noexcept(false);
  PipeProcess(const PipeProcess &) = delete;
  PipeProcess &operator=(const PipeProcess &) = delete;
  void disable();
  void resume();
  void restore();
  int wait(int &status);
};
} // namespace LIBC_NAMESPACE_DECL
#endif
