//===-- Private Linux fork recovery state -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_FORK_RECOVERY_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_FORK_RECOVERY_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Recovery capability is independent of thread identity and resource ownership.
// QuiescentChild permits loader use, not general child runtime continuation.
class ForkRecovery {
  enum class State : unsigned char { Original, ExecOnly, QuiescentChild };
  State state = State::Original;
  constexpr explicit ForkRecovery(State value) : state(value) {}

public:
  constexpr ForkRecovery() = default;
  static constexpr ForkRecovery child(bool never_threaded = false,
                                      bool loader_quiescent = false) {
    return ForkRecovery(never_threaded && loader_quiescent
                            ? State::QuiescentChild
                            : State::ExecOnly);
  }
  constexpr bool is_child() const { return state != State::Original; }
  constexpr bool allows_fork() const { return state != State::ExecOnly; }
  constexpr bool allows_thread_creation() const {
    return state == State::Original;
  }
  constexpr bool allows_loader() const { return state != State::ExecOnly; }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif
