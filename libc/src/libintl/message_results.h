//===-- Stable translated message results -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_MESSAGE_RESULTS_H
#define LLVM_LIBC_SRC_LIBINTL_MESSAGE_RESULTS_H

#include "hdr/types/size_t.h"
#include "src/__support/error_or.h"
#include "src/__support/threads/mutex.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Stable copies of distinct translated byte sequences. No locale/category or
// catalog references are retained, and lookups/errors are never cached here.
// The process instance retains results until termination; repeated translations
// (including plural counts selecting the same form) share their allocation.
class MessageResults {
  struct Entry;
  Entry *buckets[256]{};
  Mutex mutex{false, false, false, false};

public:
  MessageResults() = default;
  MessageResults(const MessageResults &) = delete;
  MessageResults &operator=(const MessageResults &) = delete;
  // Takes ownership of a malloc allocation containing size bytes plus NUL,
  // even on error. Counted bytes permit encodings with embedded zero bytes.
  ErrorOr<char *> insert(char *value, size_t size);
  // Only for quiescent disposal: all borrowed results become invalid.
  void clear();
  // This nonrecursive lock has no thread-owner identity to rebind after fork.
  bool try_lock_for_fork();
  void unlock_after_fork();
};

// Optional process-instance hooks, defined with the public message provider.
bool try_lock_message_results_for_fork();
void unlock_message_results_after_fork();

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
