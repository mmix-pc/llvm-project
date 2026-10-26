//===-- Small independent ISO-2022-CN mapping fixture ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_SUPPORT_WCHAR_ISO2022CNFIXTURE_H
#define LLVM_LIBC_TEST_SRC_SUPPORT_WCHAR_ISO2022CNFIXTURE_H

#include "src/locale/encoding_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace testing {

// Deliberately constructed repertoires keep wire-choice tests independent of
// production-table round trips. Data-source qualification is separate.
struct ISO2022CNFixture {
  uint8_t bytes[512]{};
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
  }
  ISO2022CNFixture() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 16, 322);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    const uint32_t values[][4] = {
        {0x2121, 0x4e2d, 0x2122, 0x6587},   {0x4e2d, 0x2121, 0x6587, 0x2122},
        {0x2221, 0x4e2d, 0x2222, 0x56fd},   {0x4e2d, 0x2221, 0x56fd, 0x2222},
        {0x2321, 0x20000, 0x2322, 0x20001}, {0x20000, 0x2321, 0x20001, 0x2322}};
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, i ? 322 : 130);
      put(directory + 8, i ? 0 : 192);
      put(directory + 12, i ? 0 : 6);
      uint8_t *entry = bytes + 130 + i * 16;
      put(entry, 0x201 + i);
      put(entry + 4, 2);
      put(entry + 8, 96 + i * 16);
      put(entry + 12, 16);
      for (unsigned j = 0; j != 4; ++j)
        put(bytes + 226 + i * 16 + j * 4, values[i][j]);
    }
  }
  auto read() {
    auto file = LIBC_NAMESPACE::internal::LocaleDataView::parse({bytes, 322});
    return LIBC_NAMESPACE::internal::ChineseEncodingData::read(file.value());
  }
};

} // namespace testing
} // namespace LIBC_NAMESPACE_DECL
#endif
