//===-- Implementation of psignal -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/psignal.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/file.h"
#include "src/__support/StringUtil/diagnostic_message.h"
#include "src/__support/StringUtil/signal_to_string.h"
#include "src/__support/common.h"
#include "src/__support/integer_to_string.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/stderr.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, psignal, (int signum, const char *message)) {
  const int saved_errno = libc_errno;
  // The prefix may itself be a string previously returned by strsignal.
  char buffer[sizeof("Real-time signal") + 1 +
              IntegerToString<int>::buffer_size()];
  const char *description =
      get_diagnostic_message(get_signal_string(signum, buffer).data());
  const bool has_prefix = message && *message;
  const cpp::string_view parts[] = {has_prefix ? message : "",
                                    has_prefix ? ": " : "", description, "\n"};
  File *file = reinterpret_cast<File *>(stderr);
  File::FileLock lock(file);
  for (cpp::string_view part : parts) {
    if (part.empty())
      continue;
    auto result = file->write_diagnostic_unlocked(part.data(), part.size());
    if (result.has_error()) {
      libc_errno = result.error;
      return;
    }
  }
  libc_errno = saved_errno;
}

} // namespace LIBC_NAMESPACE_DECL
