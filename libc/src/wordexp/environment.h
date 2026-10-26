//===-- Word expansion environment ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_ENVIRONMENT_H
#define LLVM_LIBC_SRC_WORDEXP_ENVIRONMENT_H

#include "src/wordexp/expand.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Per-call account lookup storage; no global cache or new process lock.
// The application synchronizes environment mutation as for getenv/setenv.
// The services do not implement command execution or diagnostic output.
class WordEnvironment {
  char *account_buffer = nullptr;
  size_t account_capacity = 0;

  static ErrorOr<WordVariable> lookup(void *, cpp::string_view);
  static int assign(void *, cpp::string_view, cpp::string_view);
  static ErrorOr<WordVariable> home(void *, cpp::string_view);
  static ErrorOr<long> read_integer(void *, cpp::string_view);
  static int write_integer(void *, cpp::string_view, long);

public:
  WordEnvironment() = default;
  WordEnvironment(const WordEnvironment &) = delete;
  WordEnvironment &operator=(const WordEnvironment &) = delete;
  ~WordEnvironment();
  WordExpansionServices services();
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
