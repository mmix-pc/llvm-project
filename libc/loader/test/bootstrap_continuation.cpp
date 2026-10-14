//===-- Test-only post-relocation continuation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/entry.h"
#include "loader/linux/arena.h"

using namespace LIBC_NAMESPACE::loader;

// Test-only no-TLS transition. Production adoption is validated separately.
extern "C" __attribute__((visibility("hidden"))) Address
__llvm_libc_loader_main_tp() { return 0; }
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_loader_after_tls() {}

// The fixture needs an actual dynamic relocation, read only after bootstrap.
// It is not a replacement for dependency loading or process startup.
extern "C" {
__attribute__((visibility("hidden"))) unsigned char loader_test_data;
__attribute__((visibility("hidden"))) const void *volatile loader_test_pointer =
    &loader_test_data;
__attribute__((visibility("hidden"))) Address
__llvm_libc_loader_after_relocation(const InitialStack *stack,
                                    const ElfImage *, Arena *bootstrap) {
  return !bootstrap->reset() && loader_test_pointer == &loader_test_data
             ? stack->value(AuxField::Entry)
             : 0;
}
}
