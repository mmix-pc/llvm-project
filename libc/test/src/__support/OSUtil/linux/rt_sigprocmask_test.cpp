//===-- Tests for the Linux kernel signal-mask boundary -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcRtSigprocmaskTest, PublicStorageAndKernelLength) {
  struct {
    sigset_t mask;
    unsigned long canary;
  } output;
  __builtin_memset(&output.mask, 0xa5, sizeof(output.mask));
  output.canary = 0x12345678;
  // Query without modifying the host's signal mask. Overlay builds exercise
  // public storage larger than the kernel mask, as well as full-build layouts.
  auto result = LIBC_NAMESPACE::linux_syscalls::rt_sigprocmask(
      SIG_SETMASK, nullptr, &output.mask);
  ASSERT_TRUE(result.has_value());
  constexpr unsigned long WORD_BITS = sizeof(unsigned long) * 8;
  constexpr unsigned long KERNEL_SIZE =
      ((NSIG - 1 + WORD_BITS - 1) / WORD_BITS) * sizeof(unsigned long);
  auto *bytes = reinterpret_cast<const unsigned char *>(&output.mask);
  for (unsigned long i = KERNEL_SIZE; i < sizeof(output.mask); ++i)
    EXPECT_EQ(bytes[i], static_cast<unsigned char>(0));
  EXPECT_EQ(output.canary, 0x12345678UL);

  __builtin_memset(&output.mask, 0xa5, sizeof(output.mask));
  sigset_t input{};
  result = LIBC_NAMESPACE::linux_syscalls::rt_sigprocmask(
      -1, &input, &output.mask);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EINVAL);
  for (unsigned long i = 0; i < sizeof(output.mask); ++i)
    EXPECT_EQ(bytes[i], static_cast<unsigned char>(0xa5));
  EXPECT_EQ(output.canary, 0x12345678UL);
}
