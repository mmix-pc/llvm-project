//===-- Linux wait status macro tests -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-macros/linux/sys-wait-macros.h"

static int check_wait_status(void) {
  for (int status = 0; status <= 0xffff; ++status) {
    int signal = status & 127;
    if (!!WIFSIGNALED(status) != (signal != 0 && signal != 127))
      return 1;
    if (!!WIFEXITED(status) != (signal == 0) ||
        !!WIFSTOPPED(status) != ((status & 255) == 127) ||
        !!WIFCONTINUED(status) != (status == 0xffff))
      return 2;
    if (WEXITSTATUS(status) != status / 256 ||
        WSTOPSIG(status) != status / 256 || WTERMSIG(status) != signal)
      return 3;
    if (!!WCOREDUMP(status) != ((status & 128) != 0))
      return 4;
    int evaluations = 0;
    if (!!WIFSIGNALED((++evaluations, status)) != (signal != 0 && signal != 127) ||
        evaluations != 1)
      return 5;
    evaluations = 0;
    if (!!WIFSTOPPED((++evaluations, status)) != ((status & 255) == 127) ||
        evaluations != 1)
      return 7;
    unsigned int unsigned_status = (unsigned int)status;
    if (!!WIFSIGNALED(unsigned_status) != !!WIFSIGNALED(status))
      return 6;
  }
  return 0;
}
