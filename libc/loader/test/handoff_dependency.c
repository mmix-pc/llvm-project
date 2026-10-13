//===-- Freestanding loader handoff dependency
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

unsigned long loader_imported_data = 42;
unsigned long loader_imported_call(unsigned long value) {
  return value + loader_imported_data;
}
