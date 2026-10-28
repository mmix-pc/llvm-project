//===-- Tests for the 48-bit random generators ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/drand48.h"
#include "src/stdlib/erand48.h"
#include "src/stdlib/jrand48.h"
#include "src/stdlib/lcong48.h"
#include "src/stdlib/lrand48.h"
#include "src/stdlib/mrand48.h"
#include "src/stdlib/nrand48.h"
#include "src/stdlib/random48_utils.h"
#include "src/stdlib/seed48.h"
#include "src/stdlib/srand48.h"
#include "test/UnitTest/Test.h"

namespace {
// Base-65536 multiplication is independent of the production packed multiply.
uint64_t advance(unsigned short x[3], const unsigned short a[3],
                 unsigned short c) {
  unsigned short next[3];
  uint64_t carry = c;
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned j = 0; j <= i; ++j)
      carry += uint64_t(x[j]) * a[i - j];
    next[i] = static_cast<unsigned short>(carry);
    carry >>= 16;
  }
  uint64_t result = 0;
  for (unsigned i = 0; i < 3; ++i) {
    x[i] = next[i];
    result |= uint64_t(next[i]) << (16 * i);
  }
  return result;
}
constexpr unsigned short DEFAULT_A[3] = {0xe66d, 0xdeec, 5};
long signed_result(uint64_t value) {
  int64_t high = value >> 16;
  return high < (int64_t(1) << 31) ? high : high - (int64_t(1) << 32);
}
} // namespace

TEST(LlvmLibcRandom48Test, DefaultAndExplicitState) {
  unsigned short zero[3] = {};
  // This is the first test and must observe the specified project default.
  EXPECT_TRUE(LIBC_NAMESPACE::drand48() == 0xb * 0x1p-48);
  for (unsigned seed = 0; seed < 4; ++seed) {
    unsigned short reference[3] = {static_cast<unsigned short>(seed), 0xffff,
                                   0x8000};
    unsigned short floating[3] = {reference[0], reference[1], reference[2]};
    unsigned short positive[3] = {reference[0], reference[1], reference[2]};
    unsigned short signed_state[3] = {reference[0], reference[1], reference[2]};
    LIBC_NAMESPACE::seed48(zero);
    for (unsigned i = 0; i < 64; ++i) {
      uint64_t expected = advance(reference, DEFAULT_A, 0xb);
      EXPECT_TRUE(LIBC_NAMESPACE::erand48(floating) ==
                  double(expected) * 0x1p-48);
      EXPECT_EQ(LIBC_NAMESPACE::nrand48(positive), long(expected >> 17));
      EXPECT_EQ(LIBC_NAMESPACE::jrand48(signed_state), signed_result(expected));
      for (unsigned j = 0; j < 3; ++j) {
        EXPECT_EQ(floating[j], reference[j]);
        EXPECT_EQ(positive[j], reference[j]);
        EXPECT_EQ(signed_state[j], reference[j]);
      }
    }
    // Explicit-state operations must not advance the shared generator.
    EXPECT_EQ(LIBC_NAMESPACE::lrand48(), 0L);
  }
}

TEST(LlvmLibcRandom48Test, InternalStateAndResets) {
  constexpr long seeds[] = {0L, 1L, -1L, 0x12345678L};
  for (long seed : seeds) {
    LIBC_NAMESPACE::srand48(seed);
    unsigned short reference[3] = {
        0x330e, static_cast<unsigned short>(seed),
        static_cast<unsigned short>(uint64_t(seed) >> 16)};
    for (unsigned i = 0; i < 32; ++i) {
      EXPECT_EQ(LIBC_NAMESPACE::lrand48(),
                long(advance(reference, DEFAULT_A, 0xb) >> 17));
      EXPECT_EQ(LIBC_NAMESPACE::mrand48(),
                signed_result(advance(reference, DEFAULT_A, 0xb)));
      EXPECT_TRUE(LIBC_NAMESPACE::drand48() ==
                  double(advance(reference, DEFAULT_A, 0xb)) * 0x1p-48);
    }
  }
  unsigned short parameters[7] = {7, 8, 9, 0xffff, 0x1234, 0xabcd, 0xffff};
  unsigned short reference[3] = {7, 8, 9}, explicit_state[3] = {7, 8, 9};
  LIBC_NAMESPACE::lcong48(parameters);
  for (unsigned i = 0; i < 64; ++i) {
    uint64_t expected = advance(reference, parameters + 3, parameters[6]);
    EXPECT_TRUE(LIBC_NAMESPACE::drand48() == double(expected) * 0x1p-48);
    EXPECT_TRUE(LIBC_NAMESPACE::erand48(explicit_state) ==
                double(expected) * 0x1p-48);
  }
  unsigned short seed[3] = {1, 2, 3};
  unsigned short *old = LIBC_NAMESPACE::seed48(seed);
  for (unsigned i = 0; i < 3; ++i)
    EXPECT_EQ(old[i], reference[i]);
  unsigned short *same = LIBC_NAMESPACE::seed48(old);
  EXPECT_EQ(same, old);
  for (unsigned i = 0; i < 3; ++i)
    EXPECT_EQ(same[i], seed[i]);
  EXPECT_TRUE(LIBC_NAMESPACE::drand48() ==
              double(advance(reference, DEFAULT_A, 0xb)) * 0x1p-48);
  LIBC_NAMESPACE::lcong48(parameters);
  LIBC_NAMESPACE::srand48(0);
  unsigned short reset[3] = {0x330e, 0, 0};
  EXPECT_EQ(LIBC_NAMESPACE::lrand48(),
            long(advance(reset, DEFAULT_A, 0xb) >> 17));
}

TEST(LlvmLibcRandom48Test, ForkFreezeDoesNotChangeState) {
  LIBC_NAMESPACE::srand48(123);
  ASSERT_TRUE(LIBC_NAMESPACE::internal::try_lock_random48_for_fork());
  EXPECT_FALSE(LIBC_NAMESPACE::internal::try_lock_random48_for_fork());
  LIBC_NAMESPACE::internal::unlock_random48_after_fork();
  unsigned short reference[3] = {0x330e, 123, 0};
  EXPECT_EQ(LIBC_NAMESPACE::lrand48(),
            long(advance(reference, DEFAULT_A, 0xb) >> 17));
}
