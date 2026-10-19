//===-- MMIX Linux process credentials --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CREDENTIALS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CREDENTIALS_H

#include "src/__support/macros/config.h"
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ThreadControl;
constexpr int CREDENTIAL_SIGNAL = 33;
constexpr uint64_t CREDENTIAL_MASK = uint64_t(1) << (CREDENTIAL_SIGNAL - 1);

// The gate protects actual kernel-task membership, independently of public
// pthread handles. Waiting for it must leave the private notification enabled.
class CredentialGate {
  uint64_t saved_mask = 0;
  bool owned = false;

public:
  CredentialGate();
  ~CredentialGate();
  CredentialGate(const CredentialGate &) = delete;
  CredentialGate &operator=(const CredentialGate &) = delete;
  bool acquired() const { return owned; }
  void release();
};

bool initialize_credentials(ThreadControl &);
// Caller holds CredentialGate. Publish before releasing a new thread to user
// code, withdraw only after all user callbacks have completed.
void register_credentials(ThreadControl &);
void unregister_credentials(ThreadControl &);
void restore_credentials_after_fork(ThreadControl &);
bool prepare_credentials_fork();
void finish_credentials_fork();
long change_credentials(unsigned long number, long first, long second = 0,
                        long third = 0);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
