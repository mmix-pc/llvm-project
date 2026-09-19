//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unittests for getaddrinfo and freeaddrinfo.
///
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/netdb_macros.h"
#include "hdr/netinet_in_macros.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/types/struct_addrinfo.h"
#include "hdr/types/struct_sockaddr_in.h"
#include "src/__support/endian_internal.h"
#include "src/netdb/freeaddrinfo.h"
#include "src/netdb/getaddrinfo.h"
#include "src/netdb/getnameinfo.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::testing::ErrnoSetterMatcher;
using LlvmLibcNetdbTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcNetdbTest, GetAddrInfoReturnsEaiSystem) {
  struct addrinfo *res = nullptr;
  ASSERT_THAT(LIBC_NAMESPACE::getaddrinfo("localhost", nullptr, nullptr, &res),
              Fails(ENOSYS, EAI_SYSTEM));
  EXPECT_EQ(res, nullptr);

  // A null result remains safe to release on failure.
  LIBC_NAMESPACE::freeaddrinfo(res);
}

TEST_F(LlvmLibcNetdbTest, NumericAddressRoundTrip) {
  addrinfo hints{};
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_CANONNAME;
  addrinfo *result = nullptr;
  ASSERT_EQ(LIBC_NAMESPACE::getaddrinfo("192.0.2.1", "65535", &hints, &result),
            0);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->ai_family, AF_INET);
  EXPECT_EQ(result->ai_socktype, SOCK_STREAM);
  EXPECT_EQ(result->ai_protocol, IPPROTO_TCP);
  EXPECT_STREQ(result->ai_canonname, "192.0.2.1");
  ASSERT_NE(result->ai_next, nullptr);
  EXPECT_EQ(result->ai_next->ai_socktype, SOCK_DGRAM);
  EXPECT_EQ(result->ai_next->ai_protocol, IPPROTO_UDP);
  EXPECT_EQ(result->ai_next->ai_next, nullptr);
  EXPECT_EQ(result->ai_next->ai_canonname, nullptr);
  char host[16], service[6];
  EXPECT_EQ(LIBC_NAMESPACE::getnameinfo(
                result->ai_addr, result->ai_addrlen, host, sizeof(host),
                service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV),
            0);
  EXPECT_STREQ(host, "192.0.2.1");
  EXPECT_STREQ(service, "65535");
  host[0] = 'x';
  EXPECT_EQ(LIBC_NAMESPACE::getnameinfo(result->ai_addr, result->ai_addrlen,
                                        host, 3, nullptr, 0, NI_NUMERICHOST),
            EAI_OVERFLOW);
  EXPECT_EQ(host[0], 'x');
  ASSERT_THAT(LIBC_NAMESPACE::getnameinfo(result->ai_addr, result->ai_addrlen,
                                          host, sizeof(host), nullptr, 0,
                                          NI_NAMEREQD),
              Fails(ENOSYS, EAI_SYSTEM));
  LIBC_NAMESPACE::freeaddrinfo(result);
}

TEST_F(LlvmLibcNetdbTest, NullHostAndProtocolSelection) {
  addrinfo hints{};
  hints.ai_protocol = IPPROTO_UDP;
  addrinfo *result = nullptr;
  ASSERT_EQ(LIBC_NAMESPACE::getaddrinfo(nullptr, "80", &hints, &result), 0);
  EXPECT_EQ(result->ai_socktype, SOCK_DGRAM);
  EXPECT_EQ(result->ai_next, nullptr);
  auto *addr = reinterpret_cast<sockaddr_in *>(result->ai_addr);
  EXPECT_EQ(LIBC_NAMESPACE::Endian::from_big_endian(addr->sin_addr.s_addr),
            uint32_t(0x7f000001));
  LIBC_NAMESPACE::freeaddrinfo(result);
  hints.ai_flags = AI_PASSIVE;
  ASSERT_EQ(LIBC_NAMESPACE::getaddrinfo(nullptr, "0", &hints, &result), 0);
  addr = reinterpret_cast<sockaddr_in *>(result->ai_addr);
  EXPECT_EQ(addr->sin_addr.s_addr, uint32_t(0));
  LIBC_NAMESPACE::freeaddrinfo(result);
}

TEST_F(LlvmLibcNetdbTest, InvalidInputsAndUnavailableNames) {
  addrinfo hints{};
  addrinfo *result = nullptr;
  hints.ai_flags = AI_NUMERICHOST;
  EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo("invalid", "0", &hints, &result),
            EAI_NONAME);
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo("127.0.0.1", "65536", &hints, &result),
            EAI_SERVICE);
  hints.ai_flags = AI_NUMERICSERV;
  EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo("127.0.0.1", "http", &hints, &result),
            EAI_NONAME);
  hints.ai_flags = 0;
  ASSERT_THAT(LIBC_NAMESPACE::getaddrinfo("127.0.0.1", "http", &hints, &result),
              Fails(ENOSYS, EAI_SYSTEM));
  hints.ai_family = AF_INET6;
  EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo("::1", "0", &hints, &result),
            EAI_FAMILY);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_UDP;
  EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo("127.0.0.1", "0", &hints, &result),
            EAI_SERVICE);
  hints = {};
  hints.ai_flags = AI_ADDRCONFIG;
  EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo("127.0.0.1", "0", &hints, &result),
            EAI_BADFLAGS);
  EXPECT_EQ(LIBC_NAMESPACE::getaddrinfo(nullptr, nullptr, nullptr, &result),
            EAI_NONAME);
}
