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
#include "loader/linux/tls_layout.h"

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

using RelocationValue = StartupRelocationError (*)(void *, size_t,
                                                   const DynamicRelocation &,
                                                   Address &);
// Reuse whole-batch preflight and RELATIVE-first writes with a caller-owned
// binding policy. The callback must be deterministic and allocation-free.
StartupRelocationResult
relocate_objects(cpp::span<const SymbolObject> objects,
                 cpp::span<const cpp::span<const WritableRegion>> writable,
                 RelocationValue resolve, void *context);

class RelocatedTLS;
StartupRelocationResult
relocate_startup_tls(cpp::span<const SymbolObject> objects,
                     cpp::span<const cpp::span<const WritableRegion>> writable,
                     const StartupTLSSet &layout, RelocatedTLS &out);

// Borrows immutable descriptors and mapped images. Only complete whole-scope
// relocation grants template-copy readiness; no startup/TP publication occurs.
class RelocatedTLS {
  const StartupTLSSet *ready = nullptr;
  friend StartupRelocationResult relocate_startup_tls(
      cpp::span<const SymbolObject>,
      cpp::span<const cpp::span<const WritableRegion>>, const StartupTLSSet &,
      RelocatedTLS &);

public:
  const StartupTLSSet *modules() const { return ready; }
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
