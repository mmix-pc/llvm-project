//===-- Freestanding ownership code generation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/module_ownership.h"

using namespace LIBC_NAMESPACE::loader;

extern "C" long acquire(ModuleOwnership *owner, Arena *arena,
                        ModuleOwnership::Kind kind,
                        ModuleOwnership::Token *token) {
  return owner->acquire(*arena, kind, *token);
}
extern "C" long release(ModuleOwnership *owner,
                        const ModuleOwnership::Token *token) {
  return owner->release(*token);
}
extern "C" long retire(ModuleOwnership *owner) { return owner->retire(); }
extern "C" long reclaimed(ModuleOwnership *owner) {
  return owner->mark_reclaimed();
}
