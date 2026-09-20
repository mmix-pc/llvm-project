//===-- Tests for Linux socket length types -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Include the public definitions directly, including in overlay builds.
#include "../../include/llvm-libc-types/socklen_t.h"
#include "../../include/llvm-libc-types/struct_addrinfo.h"
#include "../../include/llvm-libc-types/struct_msghdr.h"
#include "test/UnitTest/LibcTest.h"

// Linux reads and writes socket length pointers as 32-bit integers, regardless
// of pointer width or endianness. Small values can conceal this on little endian.
TEST(LlvmLibcSocketLengthTest, KernelABI) {
  static_assert(sizeof(socklen_t) == 4);
  static_assert(alignof(socklen_t) == alignof(unsigned int));
  static_assert(__is_same(socklen_t, unsigned int));
  static_assert(sizeof(((struct msghdr *)0)->msg_namelen) == 4);
  static_assert(sizeof(((struct addrinfo *)0)->ai_addrlen) == 4);
}
