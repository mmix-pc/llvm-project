//===-- Small collation table for pattern tests -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_LOCALE_COLLATION_FIXTURE_H
#define LLVM_LIBC_TEST_SRC_LOCALE_COLLATION_FIXTURE_H

#include "src/locale/collation_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace testing {
using LIBC_NAMESPACE::internal::CollationData;
using LIBC_NAMESPACE::internal::LocaleDataView;
struct PatternCollationFixture {
  uint8_t bytes[1024]{};
  static void put(uint8_t *where, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      where[i] = uint8_t(value >> (24 - 8 * i));
  }
  auto read() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = magic[i];
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    const uint32_t records[][15] = {
        {5, 8, 1, 1, 1, 1, 1},
        {1, 'a', 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {2, 'a', 'b', 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1},
        {1, 'b', 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1},
        {2, 'b', 'a', 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1},
        {1, 0xe4, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1}};
    const unsigned lengths[] = {7, 14, 15, 14, 15, 14};
    size_t size = 96;
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *entry = bytes + 130 + i * 16;
      put(entry, i ? 0x1000 + i - 1 : 1);
      put(entry + 4, 2);
      put(entry + 8, uint32_t(size));
      put(entry + 12, lengths[i] * 4);
      for (unsigned j = 0; j != lengths[i]; ++j)
        put(bytes + 130 + size + j * 4, records[i][j]);
      size += lengths[i] * 4;
    }
    put(bytes + 16, uint32_t(130 + size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *entry = bytes + 32 + i * 16;
      put(entry, i);
      put(entry + 4, uint32_t(i <= 3 ? 130 : 130 + size));
      put(entry + 8, uint32_t(i == 3 ? size : 0));
      put(entry + 12, i == 3 ? 6 : 0);
    }
    auto view = LocaleDataView::parse({bytes, 130 + size});
    if (!view)
      return ErrorOr<CollationData>(Error(view.error()));
    return CollationData::read(*view);
  }
};
} // namespace testing
} // namespace LIBC_NAMESPACE_DECL
#endif
