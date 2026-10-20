//===-- DNS query tests ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/resolv_macros.h"
#include "hdr/types/ns_flag.h"
#include "hdr/types/ssize_t.h"
#include "src/__support/error_or.h"
#include "src/__support/libc_errno.h"
#include "src/resolv/ns_initparse.h"
#include "src/resolv/ns_name_uncompress.h"
#include "src/resolv/ns_parserr.h"
#include "src/resolv/query.h"
#include "src/resolv/res_mkquery.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
namespace {

struct FixedRandom {
  int calls = 0;
  ErrorOr<ssize_t> operator()(void *buffer, size_t size, unsigned int flags) {
    ++calls;
    if (size != 2 || flags != 0)
      return Error(EINVAL);
    unsigned char *p = static_cast<unsigned char *>(buffer);
    p[0] = 0x12;
    p[1] = 0x34;
    return 2;
  }
};

int query(const char *name, unsigned char *buffer, int size,
          FixedRandom &random) {
  return dns::make_query(QUERY, name, C_IN, T_A, nullptr, 0, nullptr, buffer,
                         size, random);
}

} // namespace

TEST(LlvmLibcDnsQuery, CompletePacketAndPublicParsing) {
  FixedRandom random;
  unsigned char packet[25];
  for (auto &byte : packet)
    byte = 0xa5;
  libc_errno = EDOM;
  ASSERT_EQ(24, query("x.test.", packet, 24, random));
  EXPECT_EQ(1, random.calls);
  EXPECT_EQ(EDOM, static_cast<int>(libc_errno));
  EXPECT_EQ(0xa5, static_cast<int>(packet[24]));
  constexpr unsigned char expected[] = {0x12, 0x34, 1,   0, 0, 1,   0, 0,
                                        0,    0,    0,   0, 1, 'x', 4, 't',
                                        'e',  's',  't', 0, 0, 1,   0, 1};
  for (unsigned int i = 0; i < sizeof(expected); ++i)
    EXPECT_EQ(expected[i], packet[i]);
  ns_msg msg;
  ns_rr rr;
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_initparse(packet, 24, &msg));
  EXPECT_EQ(0x1234, ns_msg_id(msg));
  EXPECT_EQ(1, ns_msg_count(msg, ns_s_qd));
  EXPECT_EQ(0, ns_msg_count(msg, ns_s_an));
  EXPECT_EQ(0, ns_msg_count(msg, ns_s_ns));
  EXPECT_EQ(0, ns_msg_count(msg, ns_s_ar));
  EXPECT_EQ(0x100, static_cast<int>(msg._flags));
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_qd, 0, &rr));
  EXPECT_STREQ("x.test", ns_rr_name(rr));
  EXPECT_EQ(ns_t_a, ns_rr_type(rr));
  EXPECT_EQ(ns_c_in, ns_rr_class(rr));
}

TEST(LlvmLibcDnsQuery, PublicQueryUsesSystemEntropy) {
  unsigned char packet[24];
  libc_errno = EDOM;
  ASSERT_EQ(24,
            LIBC_NAMESPACE::res_mkquery(QUERY, "x.test", C_IN, T_A, nullptr, 0,
                                        nullptr, packet, sizeof(packet)));
  EXPECT_EQ(EDOM, static_cast<int>(libc_errno));
  ns_msg msg;
  ns_rr rr;
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_initparse(packet, sizeof(packet), &msg));
  EXPECT_EQ(0x100, static_cast<int>(msg._flags));
  EXPECT_EQ(1, ns_msg_count(msg, ns_s_qd));
  EXPECT_EQ(0, ns_msg_count(msg, ns_s_an));
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_qd, 0, &rr));
  EXPECT_STREQ("x.test", ns_rr_name(rr));
  EXPECT_EQ(ns_t_a, ns_rr_type(rr));
  EXPECT_EQ(ns_c_in, ns_rr_class(rr));
}

TEST(LlvmLibcDnsQuery, BufferBoundsAndNoPartialWrites) {
  FixedRandom random;
  unsigned char packet[25];
  for (auto &byte : packet)
    byte = 0xa5;
  for (int size = -1; size < 24; ++size) {
    EXPECT_EQ(-1, query("x.test", packet, size, random));
    EXPECT_EQ(EMSGSIZE, static_cast<int>(libc_errno));
    for (auto byte : packet)
      EXPECT_EQ(0xa5, static_cast<int>(byte));
  }
  EXPECT_EQ(0, random.calls);
  EXPECT_EQ(24, query("x.test", packet, 24, random));
  EXPECT_EQ(0xa5, static_cast<int>(packet[24]));
}

TEST(LlvmLibcDnsQuery, RootEscapesAndFullWidthQuestionFields) {
  FixedRandom random;
  unsigned char packet[512];
  const char *roots[] = {"", "."};
  for (const char *root : roots) {
    ASSERT_EQ(17, query(root, packet, sizeof(packet), random));
    EXPECT_EQ(0, static_cast<int>(packet[12]));
  }
  constexpr char escaped[] = "a\\.b.\\000\\255\\\\\\@\\032";
  int size = query(escaped, packet, sizeof(packet), random);
  ASSERT_GT(size, 0);
  char out[NS_MAXDNAME];
  EXPECT_EQ(size - NS_HFIXEDSZ - NS_QFIXEDSZ,
            LIBC_NAMESPACE::ns_name_uncompress(
                packet, packet + size, packet + NS_HFIXEDSZ, out, sizeof(out)));
  EXPECT_STREQ(escaped, out);
  ASSERT_EQ(17, dns::make_query(QUERY, ".", 65535, 65535, nullptr, 0, nullptr,
                                packet, sizeof(packet), random));
  for (int i = 13; i < 17; ++i)
    EXPECT_EQ(255, static_cast<int>(packet[i]));
}

TEST(LlvmLibcDnsQuery, MaximumWireAndPresentationNames) {
  FixedRandom random;
  unsigned char packet[272];
  for (auto &byte : packet)
    byte = 0xa5;
  char name[1006];
  unsigned int pos = 0;
  for (int label = 0; label < 4; ++label) {
    if (label)
      name[pos++] = '.';
    unsigned int length = label == 3 ? 61 : 63;
    for (unsigned int i = 0; i < length; ++i) {
      name[pos++] = '\\';
      name[pos++] = '2';
      name[pos++] = '5';
      name[pos++] = '5';
    }
  }
  name[pos] = 0;
  ASSERT_EQ(1003U, pos);
  ASSERT_EQ(271, query(name, packet, 271, random));
  EXPECT_EQ(0xa5, static_cast<int>(packet[271]));
  char out[NS_MAXDNAME];
  ASSERT_EQ(255, LIBC_NAMESPACE::ns_name_uncompress(
                     packet, packet + 271, packet + 12, out, sizeof(out)));
  EXPECT_STREQ(name, out);
  name[pos++] = 'x'; // 256 wire octets, even though the final label fits.
  name[pos] = 0;
  EXPECT_EQ(-1, query(name, packet, sizeof(packet), random));
  EXPECT_EQ(EMSGSIZE, static_cast<int>(libc_errno));
  char long_label[65];
  for (unsigned int i = 0; i < 64; ++i)
    long_label[i] = 'x';
  long_label[64] = 0;
  EXPECT_EQ(-1, query(long_label, packet, sizeof(packet), random));
  EXPECT_EQ(EMSGSIZE, static_cast<int>(libc_errno));
}

TEST(LlvmLibcDnsQuery, InvalidNamesAndPublicUnsupportedArguments) {
  FixedRandom random;
  unsigned char packet[512];
  for (auto &byte : packet)
    byte = 0xa5;
  const char *invalid[] = {".x",   "x..y",  "x..",   "\\",   "x.\\1",
                           "\\12", "\\1x2", "\\256", "\\999"};
  for (const char *name : invalid) {
    EXPECT_EQ(-1, query(name, packet, sizeof(packet), random));
    EXPECT_EQ(EINVAL, static_cast<int>(libc_errno));
  }
  EXPECT_EQ(0, random.calls);
  EXPECT_EQ(-1, LIBC_NAMESPACE::res_mkquery(IQUERY, "x", C_IN, T_A, nullptr, 0,
                                            nullptr, packet, sizeof(packet)));
  EXPECT_EQ(EINVAL, static_cast<int>(libc_errno));
  EXPECT_EQ(-1, LIBC_NAMESPACE::res_mkquery(QUERY, "x", C_IN, T_A, packet, 0,
                                            nullptr, packet, sizeof(packet)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::res_mkquery(QUERY, "x", C_IN, T_A, nullptr, 1,
                                            nullptr, packet, sizeof(packet)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::res_mkquery(QUERY, "x", C_IN, T_A, nullptr, 0,
                                            packet, packet, sizeof(packet)));
  EXPECT_EQ(-1,
            LIBC_NAMESPACE::res_mkquery(QUERY, nullptr, C_IN, T_A, nullptr, 0,
                                        nullptr, packet, sizeof(packet)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::res_mkquery(QUERY, "x", C_IN, T_A, nullptr, 0,
                                            nullptr, nullptr, 512));
  EXPECT_EQ(-1, LIBC_NAMESPACE::res_mkquery(QUERY, "x", -1, T_A, nullptr, 0,
                                            nullptr, packet, sizeof(packet)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::res_mkquery(QUERY, "x", C_IN, 65536, nullptr, 0,
                                            nullptr, packet, sizeof(packet)));
  for (auto byte : packet)
    EXPECT_EQ(0xa5, static_cast<int>(byte));
}

TEST(LlvmLibcDnsQuery, InterruptedAndPartialEntropyReads) {
  int calls = 0;
  auto random = [&](void *dst, size_t size,
                    unsigned int flags) -> ErrorOr<ssize_t> {
    ++calls;
    EXPECT_EQ(0U, flags);
    if (calls == 1)
      return Error(EINTR);
    EXPECT_EQ(static_cast<size_t>(4 - calls), size);
    *static_cast<unsigned char *>(dst) = calls == 2 ? 0xab : 0xcd;
    return 1;
  };
  unsigned char packet[17];
  libc_errno = EDOM;
  ASSERT_EQ(17, dns::make_query(QUERY, ".", C_IN, T_A, nullptr, 0, nullptr,
                                packet, sizeof(packet), random));
  EXPECT_EQ(3, calls);
  EXPECT_EQ(0xab, static_cast<int>(packet[0]));
  EXPECT_EQ(0xcd, static_cast<int>(packet[1]));
  EXPECT_EQ(EDOM, static_cast<int>(libc_errno));
}

TEST(LlvmLibcDnsQuery, EntropyFailureNeverFallsBackOrWrites) {
  unsigned char packet[17];
  for (auto &byte : packet)
    byte = 0xa5;
  auto unavailable = [](void *, size_t, unsigned int) -> ErrorOr<ssize_t> {
    return Error(ENOSYS);
  };
  EXPECT_EQ(-1, dns::make_query(QUERY, ".", C_IN, T_A, nullptr, 0, nullptr,
                                packet, sizeof(packet), unavailable));
  EXPECT_EQ(ENOSYS, static_cast<int>(libc_errno));
  auto no_progress = [](void *, size_t, unsigned int) -> ErrorOr<ssize_t> {
    return 0;
  };
  EXPECT_EQ(-1, dns::make_query(QUERY, ".", C_IN, T_A, nullptr, 0, nullptr,
                                packet, sizeof(packet), no_progress));
  EXPECT_EQ(EIO, static_cast<int>(libc_errno));
  int calls = 0;
  auto partial_failure = [&](void *dst, size_t,
                             unsigned int) -> ErrorOr<ssize_t> {
    if (++calls == 1) {
      *static_cast<unsigned char *>(dst) = 0x12;
      return 1;
    }
    return Error(EAGAIN);
  };
  EXPECT_EQ(-1, dns::make_query(QUERY, ".", C_IN, T_A, nullptr, 0, nullptr,
                                packet, sizeof(packet), partial_failure));
  EXPECT_EQ(EAGAIN, static_cast<int>(libc_errno));
  EXPECT_EQ(2, calls);
  for (auto byte : packet)
    EXPECT_EQ(0xa5, static_cast<int>(byte));
}

} // namespace LIBC_NAMESPACE_DECL
