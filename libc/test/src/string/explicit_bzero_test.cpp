//===-- Tests for explicit_bzero ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-macros/sys-param-macros.h"
#include "src/string/explicit_bzero.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcExplicitBzeroTest, UnalignedLengthsAndSurroundingBytes) {
  unsigned char bytes[259];
  for (unsigned start = 0; start < 3; ++start) {
    for (unsigned size = 0; size <= 256; ++size) {
      for (auto &byte : bytes)
        byte = 0xa5;
      LIBC_NAMESPACE::explicit_bzero(bytes + start, size);
      for (unsigned i = 0; i < sizeof(bytes); ++i)
        ASSERT_EQ(bytes[i], static_cast<unsigned char>(
                                i >= start && i < start + size ? 0 : 0xa5));
    }
  }
}

TEST(LlvmLibcExplicitBzeroTest, BitmapByteBoundaries) {
  for (unsigned bit = 0; bit < 3 * NBBY; ++bit) {
    unsigned char bytes[3] = {};
    ASSERT_TRUE(isclr(bytes, bit));
    setbit(bytes, bit);
    for (unsigned i = 0; i < 3 * NBBY; ++i)
      ASSERT_EQ(!!isset(bytes, i), i == bit);
    clrbit(bytes, bit);
    for (auto byte : bytes)
      ASSERT_EQ(byte, static_cast<unsigned char>(0));
  }
}
