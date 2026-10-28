//===-- Tests for BSD random state switching ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/stdlib/initstate.h"
#include "src/stdlib/random.h"
#include "src/stdlib/random_utils.h"
#include "src/stdlib/setstate.h"
#include "src/stdlib/srandom.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <stdint.h>

TEST(LlvmLibcRandomTest, DefaultEqualsSeedOne) {
  long initial[64];
  for (long &value : initial)
    value = LIBC_NAMESPACE::random();
  alignas(8) char storage[128];
  char *original = LIBC_NAMESPACE::initstate(1, storage, sizeof(storage));
  ASSERT_NE(original, nullptr);
  for (long value : initial)
    EXPECT_EQ(value, LIBC_NAMESPACE::random());
  EXPECT_EQ(LIBC_NAMESPACE::setstate(original), storage);
}

TEST(LlvmLibcRandomTest, BoundariesUnalignedAndRepeatable) {
  constexpr size_t sizes[] = {8,  9,   31,  32,  33,  63,  64,
                              65, 127, 128, 129, 255, 256, 257};
  constexpr unsigned seeds[] = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff};
  for (size_t size : sizes) {
    for (unsigned seed : seeds) {
      alignas(8) unsigned char storage[260];
      for (auto &byte : storage)
        byte = 0xa5;
      char *buffer = reinterpret_cast<char *>(storage + 1);
      char *original = LIBC_NAMESPACE::initstate(seed, buffer, size);
      ASSERT_NE(original, nullptr);
      long expected[40];
      for (long &value : expected) {
        value = LIBC_NAMESPACE::random();
        EXPECT_GE(value, 0L);
        EXPECT_LE(value, 0x7fffffffL);
      }
      EXPECT_EQ(unsigned(storage[0]), 0xa5U);
      for (size_t i = size + 1; i < sizeof(storage); ++i)
        EXPECT_EQ(unsigned(storage[i]), 0xa5U);
      LIBC_NAMESPACE::srandom(seed);
      for (long value : expected)
        EXPECT_EQ(value, LIBC_NAMESPACE::random());
      EXPECT_EQ(LIBC_NAMESPACE::setstate(original), buffer);
    }
  }
}

TEST(LlvmLibcRandomTest, ResumeAndSameBuffer) {
  alignas(8) char a[128], b[256];
  char *original = LIBC_NAMESPACE::initstate(7, a, sizeof(a));
  long expected[20];
  for (long &value : expected)
    value = LIBC_NAMESPACE::random();
  LIBC_NAMESPACE::srandom(7);
  for (unsigned i = 0; i < 7; ++i)
    EXPECT_EQ(LIBC_NAMESPACE::random(), expected[i]);
  EXPECT_EQ(LIBC_NAMESPACE::initstate(9, b, sizeof(b)), a);
  long other = LIBC_NAMESPACE::random();
  EXPECT_EQ(LIBC_NAMESPACE::setstate(a), b);
  EXPECT_EQ(LIBC_NAMESPACE::setstate(a), a);
  for (unsigned i = 7; i < 20; ++i)
    EXPECT_EQ(LIBC_NAMESPACE::random(), expected[i]);
  EXPECT_EQ(LIBC_NAMESPACE::setstate(b), a);
  LIBC_NAMESPACE::srandom(9);
  EXPECT_EQ(LIBC_NAMESPACE::random(), other);
  EXPECT_EQ(LIBC_NAMESPACE::setstate(original), b);
}

TEST(LlvmLibcRandomTest, FailedSwitchPreservesStream) {
  alignas(8) char state[128];
  char *original = LIBC_NAMESPACE::initstate(19, state, sizeof(state));
  long first = LIBC_NAMESPACE::random(), second = LIBC_NAMESPACE::random();
  LIBC_NAMESPACE::srandom(19);
  EXPECT_EQ(LIBC_NAMESPACE::random(), first);
  unsigned char short_state[7] = {1, 2, 3, 4, 5, 6, 7};
  libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::initstate(1, reinterpret_cast<char *>(short_state),
                                      sizeof(short_state)),
            nullptr);
  EXPECT_EQ(int(libc_errno), EINVAL);
  for (unsigned i = 0; i < sizeof(short_state); ++i)
    EXPECT_EQ(unsigned(short_state[i]), i + 1);
  uint32_t invalid = UINT32_MAX;
  EXPECT_EQ(LIBC_NAMESPACE::setstate(reinterpret_cast<char *>(&invalid)),
            nullptr);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::random(), second);
  EXPECT_EQ(LIBC_NAMESPACE::setstate(original), state);
}

TEST(LlvmLibcRandomTest, CongruentialReferenceAndForkFreeze) {
  char state[8];
  char *original = LIBC_NAMESPACE::initstate(0xffffffff, state, sizeof(state));
  uint64_t expected = 0xffffffff;
  ASSERT_TRUE(LIBC_NAMESPACE::internal::try_lock_random_for_fork());
  EXPECT_FALSE(LIBC_NAMESPACE::internal::try_lock_random_for_fork());
  LIBC_NAMESPACE::internal::unlock_random_after_fork();
  for (unsigned i = 0; i < 64; ++i) {
    expected = (expected * 1103515245 + 12345) % (uint64_t(1) << 31);
    EXPECT_EQ(LIBC_NAMESPACE::random(), static_cast<long>(expected));
  }
  EXPECT_EQ(LIBC_NAMESPACE::setstate(original), state);
}
