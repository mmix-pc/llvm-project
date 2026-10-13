//===-- Eager startup relocation ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RELOCATION_H
#define LLVM_LIBC_LOADER_LINUX_RELOCATION_H

#include "loader/linux/self_relocation.h"
#include "loader/linux/symbols.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

enum class StartupRelocationError {
  None,
  Image,
  Metadata,
  Destination,
  Symbol,
  Value
};
struct StartupRelocationResult {
  StartupRelocationError error;
  size_t object;
  Address relocation;
};

// Borrow a completed main-first scope and matching writable mapped views.
// Metadata is immutable and paired with its image; objects must not alias.
// Preflight the entire scope before any writes. No concurrent mutation, user
// execution, protection changes or interpreter self-relocation is permitted.
StartupRelocationResult
relocate_startup(cpp::span<const SymbolObject> objects,
                 cpp::span<const cpp::span<const WritableRegion>> writable);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_RELOCATION_H
