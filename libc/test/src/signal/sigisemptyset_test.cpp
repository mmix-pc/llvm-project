//===-- Signal-set emptiness tests ----------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "hdr/signal_macros.h"
#include "src/__support/libc_errno.h"
#include "src/signal/sigisemptyset.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcSigisemptysetTest, BitsAndPreservation) {
  sigset_t set{};
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sigisemptyset(&set), 1);
  constexpr unsigned bits = sizeof(unsigned long) * 8;
  for (unsigned bit = 0; bit < NSIG - 1; ++bit) {
    set = {};
    unsigned long value = 1UL << (bit % bits);
    __builtin_memcpy(reinterpret_cast<unsigned char *>(&set) +
                         (bit / bits) * sizeof(value),
                     &value, sizeof(value));
    sigset_t before = set;
    EXPECT_EQ(LIBC_NAMESPACE::sigisemptyset(&set), 0);
    EXPECT_EQ(__builtin_memcmp(&set, &before, sizeof(set)), 0);
    EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), EDOM);
  }
}
TEST(LlvmLibcSigisemptysetTest, ReservedStorageAndNull) {
  sigset_t set{};
  constexpr unsigned used = ((NSIG - 1 + sizeof(unsigned long) * 8 - 1) /
                             (sizeof(unsigned long) * 8)) *
                            sizeof(unsigned long);
  for (unsigned i = used; i < sizeof(set); ++i)
    reinterpret_cast<unsigned char *>(&set)[i] = 255;
  EXPECT_EQ(LIBC_NAMESPACE::sigisemptyset(&set), 1);
  EXPECT_EQ(LIBC_NAMESPACE::sigisemptyset(nullptr), -1);
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), EINVAL);
}
