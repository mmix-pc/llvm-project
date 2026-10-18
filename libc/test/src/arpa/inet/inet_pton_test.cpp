//===-- Unittests for inet_pton
//--------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_socket_macros.h"
#include "src/arpa/inet/inet_pton.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"

using LlvmLibcInetPtonTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcInetPtonTest, UnsupportedFamily) {
  EXPECT_EQ(-1, LIBC_NAMESPACE::inet_pton(-1, "", nullptr));
  ASSERT_ERRNO_EQ(EAFNOSUPPORT);
}

TEST_F(LlvmLibcInetPtonTest, NetworkOrderAndUnalignedOutput) {
  unsigned char bytes[18] = {};
  EXPECT_EQ(1, LIBC_NAMESPACE::inet_pton(AF_INET, "192.0.2.255", bytes + 1));
  EXPECT_EQ(int(bytes[0]), 0);
  EXPECT_EQ(int(bytes[1]), 192);
  EXPECT_EQ(int(bytes[2]), 0);
  EXPECT_EQ(int(bytes[3]), 2);
  EXPECT_EQ(int(bytes[4]), 255);
  EXPECT_EQ(int(bytes[5]), 0);
  const unsigned char expected[16] = {0x20, 1, 0xd, 0xb8, 0,    0, 0, 0,
                                      0,    0, 0,   0,    0xc0, 0, 2, 1};
  EXPECT_EQ(
      1, LIBC_NAMESPACE::inet_pton(AF_INET6, "2001:DB8::192.0.2.1", bytes + 1));
  for (unsigned i = 0; i < 16; ++i)
    EXPECT_EQ(bytes[i + 1], expected[i]);
  EXPECT_EQ(int(bytes[0]), 0);
  EXPECT_EQ(int(bytes[17]), 0);
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcInetPtonTest, Compression) {
  struct {
    const char *text;
    unsigned char bytes[16];
  } cases[] = {
      {"::", {}},
      {"::1", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}},
      {"1::", {0, 1}},
      {"1:2:3:4:5:6:7:8", {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8}},
      {"1:2:3:4:5:6::8", {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 0, 0, 8}},
      {"::ffff:255.255.255.255",
       {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255}}};
  for (const auto &entry : cases) {
    unsigned char bytes[16];
    ASSERT_EQ(1, LIBC_NAMESPACE::inet_pton(AF_INET6, entry.text, bytes));
    for (unsigned i = 0; i < 16; ++i)
      EXPECT_EQ(bytes[i], entry.bytes[i]);
  }
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcInetPtonTest, RejectsInvalidWithoutWriting) {
  const char *ipv4[] = {"",           "1",        "127.1",
                        "0x7f.0.0.1", "01.2.3.4", "1.2.3.256",
                        "1.2.3.-1",   "1.2.3.4 ", "1.2.3.4.5",
                        "1..2.3",     "1.2.3.",   "1.2.3.42949672960"};
  const char *ipv6[] = {"",
                        ":",
                        "1:",
                        ":::1",
                        "1::2::3",
                        "1:2:3:4:5:6:7",
                        "1:2:3:4:5:6:7:8:9",
                        "1:2:3:4:5:6:7:8::",
                        "::1:2:3:4:5:6:7:8",
                        "::12345",
                        "::g",
                        "::1%eth0",
                        "::ffff:01.2.3.4",
                        "1:2:3:4:5:6:7:192.0.2.1"};
  auto check = [](int af, const char *text) {
    unsigned char bytes[16];
    for (auto &byte : bytes)
      byte = 0xa5;
    EXPECT_EQ(0, LIBC_NAMESPACE::inet_pton(af, text, bytes));
    for (auto byte : bytes)
      EXPECT_EQ(int(byte), 0xa5);
  };
  for (const char *text : ipv4)
    check(AF_INET, text);
  for (const char *text : ipv6)
    check(AF_INET6, text);
  ASSERT_ERRNO_SUCCESS();
}
