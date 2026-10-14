//===-- Startup symbol binding -----------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_SYMBOLS_H
#define LLVM_LIBC_LOADER_LINUX_SYMBOLS_H

#include "loader/linux/arena.h"
#include "loader/linux/dynamic.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

class StartupGraph;

struct SymbolObject {
  const ElfImage *image;
  const DynamicInfo *metadata;
};
enum class SymbolError { None, Scope, Index, Undefined, Address, Type };
struct SymbolValue {
  // Ordinary address, or module-relative offset when tls is true.
  Address address = 0;
  size_t object = 0;
  Address symbol = 0;
  bool weak_zero = false;
  bool tls = false;
};
struct SymbolFailure {
  size_t object = 0;
  Address symbol = 0;
};

// Borrows immutable admitted image/metadata pairs in main-first BFS order.
// The graph and supplied descriptor arena must outlive every use. This is
// startup-only binding, not a public dlsym/handle or symbol-version interface.
class StartupSymbols {
  cpp::span<const SymbolObject> objects;
  SymbolError definition(size_t object, Address index, SymbolValue &out) const;

public:
  // Explicit views also permit file-based producer validation without mapping
  // target virtual addresses into the native test process.
  static SymbolError open(cpp::span<const SymbolObject> objects,
                          StartupSymbols &out, bool allow_tls = false);
  static long open(const StartupGraph &graph, Arena &arena,
                   StartupSymbols &out);
  SymbolError lookup(Bytes name, SymbolValue &out) const;
  SymbolError resolve(size_t requester, Address index, SymbolValue &out) const;
  // Validate all symbolic relocation references before relocation/initializers.
  // Unreferenced undefined dynamic symbols do not create a startup obligation.
  SymbolError validate_references(SymbolFailure &failure) const;
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_SYMBOLS_H
