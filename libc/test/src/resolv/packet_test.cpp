//===-- Public DNS packet interface tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/resolv_macros.h"
#include "hdr/types/ns_flag.h"
#include "src/__support/libc_errno.h"
#include "src/resolv/dn_expand.h"
#include "src/resolv/dn_skipname.h"
#include "src/resolv/ns_get16.h"
#include "src/resolv/ns_get32.h"
#include "src/resolv/ns_initparse.h"
#include "src/resolv/ns_name_uncompress.h"
#include "src/resolv/ns_parserr.h"
#include "src/resolv/ns_skiprr.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
namespace {

constexpr unsigned char RESPONSE[] = {
    0x12, 0x34, 0x85, 0x80, 0,    1,    0, 2, 0,   1, 0,    1,  1,    'x',
    4,    't',  'e',  's',  't',  0,    0, 1, 0,   1, 0xc0, 12, 0,    1,
    0,    1,    0xfe, 0xdc, 0xba, 0x98, 0, 4, 192, 0, 2,    1,  0xc0, 12,
    0,    15,   0,    1,    0,    0,    0, 0, 0,   4, 0,    10, 0xc0, 12,
    0xc0, 12,   0,    2,    0,    1,    0, 0, 0,   0, 0,    2,  0xc0, 12,
    0,    0,    41,   0x10, 0,    0,    0, 0, 0,   0, 0};

} // namespace

TEST(LlvmLibcDnsPacket, IntegerReaders) {
  const unsigned char bytes[] = {0, 0xfe, 0xdc, 0xba, 0x98};
  EXPECT_EQ(0xfedcU, LIBC_NAMESPACE::ns_get16(bytes + 1));
  EXPECT_EQ(0xfedcba98UL, LIBC_NAMESPACE::ns_get32(bytes + 1));
  const unsigned char *p = bytes + 1;
  unsigned int small = 0;
  unsigned long large = 0;
  NS_GET16(small, p);
  EXPECT_EQ(0xfedcU, small);
  EXPECT_EQ(p, bytes + 3);
  p = bytes + 1;
  NS_GET32(large, p);
  EXPECT_EQ(0xfedcba98UL, large);
  EXPECT_EQ(p, bytes + sizeof(bytes));
}

TEST(LlvmLibcDnsPacket, NamesAndConsumedBytes) {
  const unsigned char wire[] = {4,   't', 'e', 's',  't', 0,    3,
                                'w', 'w', 'w', 0xc0, 0,   0xc0, 6};
  const unsigned char *end = wire + sizeof(wire);
  char out[NS_MAXDNAME];
  ASSERT_EQ(
      6, LIBC_NAMESPACE::ns_name_uncompress(wire, end, wire, out, sizeof(out)));
  EXPECT_STREQ("test", out);
  ASSERT_EQ(6, LIBC_NAMESPACE::ns_name_uncompress(wire, end, wire + 6, out,
                                                  sizeof(out)));
  EXPECT_STREQ("www.test", out);
  ASSERT_EQ(2, LIBC_NAMESPACE::ns_name_uncompress(wire, end, wire + 12, out,
                                                  sizeof(out)));
  EXPECT_STREQ("www.test", out);
  ASSERT_EQ(1, LIBC_NAMESPACE::ns_name_uncompress(wire, end, wire + 5, out, 2));
  EXPECT_STREQ(".", out);
  EXPECT_EQ(-1,
            LIBC_NAMESPACE::ns_name_uncompress(wire, end, wire + 5, out, 1));
  ASSERT_EQ(1, LIBC_NAMESPACE::dn_expand(wire, end, wire + 5, out, 1));
  EXPECT_STREQ("", out);
  ASSERT_EQ(2, LIBC_NAMESPACE::dn_expand(wire, end, wire + 12, out, 9));
  EXPECT_STREQ("www.test", out);
  EXPECT_EQ(-1, LIBC_NAMESPACE::dn_expand(wire, end, wire + 12, out, 8));
  EXPECT_EQ(-1, LIBC_NAMESPACE::dn_expand(wire, end, wire, out, -1));
  EXPECT_EQ(6, LIBC_NAMESPACE::dn_skipname(wire + 6, end));
  EXPECT_EQ(2, LIBC_NAMESPACE::dn_skipname(wire + 12, end));
}

TEST(LlvmLibcDnsPacket, PresentationEscapingAndExactCapacity) {
  const unsigned char wire[] = {12,  '.', '\\', ';', '(', ')', '@',
                                '$', '"', 0,    32,  127, 255, 0};
  constexpr char expected[] = "\\.\\\\\\;\\(\\)\\@\\$\\\"\\000\\032\\127\\255";
  char out[sizeof(expected) + 1];
  out[sizeof(expected)] = 'Z';
  EXPECT_EQ(static_cast<int>(sizeof(wire)),
            LIBC_NAMESPACE::ns_name_uncompress(wire, wire + sizeof(wire), wire,
                                               out, sizeof(expected)));
  EXPECT_STREQ(expected, out);
  EXPECT_EQ('Z', out[sizeof(expected)]);
  EXPECT_EQ(-1,
            LIBC_NAMESPACE::ns_name_uncompress(wire, wire + sizeof(wire), wire,
                                               out, sizeof(expected) - 1));
  EXPECT_EQ('Z', out[sizeof(expected)]);
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_name_uncompress(wire, wire + sizeof(wire),
                                                   wire, out, 0));
}

TEST(LlvmLibcDnsPacket, MalformedNames) {
  const unsigned char invalid[][6] = {
      {0xc0},             // Truncated pointer.
      {0xc0, 2},          // Target at end.
      {0xc0, 0},          // Self reference.
      {0xc0, 2, 0xc0, 0}, // Pointer-only cycle.
      {1, 'x', 0xc0, 0},  // Cycle that also emits a label.
      {0xff, 0xff},       // Out-of-packet offset.
      {0x40, 0},
      {0x80, 0}, // Reserved label encodings.
      {3, 'x', 'y'},
      {1, 'x'}}; // Truncated label/root.
  const int lengths[] = {1, 2, 2, 4, 4, 2, 2, 2, 3, 2};
  char out[NS_MAXDNAME];
  for (unsigned int i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
    libc_errno = 0;
    EXPECT_EQ(-1, LIBC_NAMESPACE::ns_name_uncompress(
                      invalid[i], invalid[i] + lengths[i], invalid[i], out,
                      sizeof(out)));
    EXPECT_EQ(EMSGSIZE, static_cast<int>(libc_errno));
  }
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_name_uncompress(invalid[0], invalid[0] + 1,
                                                   invalid[0] + 1, out,
                                                   sizeof(out)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_name_uncompress(
                    invalid[0] + 1, invalid[0], invalid[0], out, sizeof(out)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_name_uncompress(
                    invalid[0], invalid[0], invalid[0], out, sizeof(out)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::dn_skipname(invalid[0], invalid[0] + 1));
  EXPECT_EQ(-1, LIBC_NAMESPACE::dn_skipname(invalid[6], invalid[6] + 2));
  EXPECT_EQ(-1, LIBC_NAMESPACE::dn_skipname(invalid[8], invalid[8] + 3));
  // A base-less skip must not dereference a compression target.
  EXPECT_EQ(2, LIBC_NAMESPACE::dn_skipname(invalid[5], invalid[5] + 2));
}

TEST(LlvmLibcDnsPacket, MaximumWireName) {
  unsigned char wire[257] = {};
  unsigned int pos = 0;
  for (int label = 0; label < 4; ++label) {
    unsigned int length = label == 3 ? 61 : 63;
    wire[pos++] = static_cast<unsigned char>(length);
    for (unsigned int i = 0; i < length; ++i)
      wire[pos++] = 255;
  }
  wire[pos++] = 0;
  ASSERT_EQ(255U, pos);
  char out[NS_MAXDNAME];
  EXPECT_EQ(255, LIBC_NAMESPACE::ns_name_uncompress(wire, wire + pos, wire, out,
                                                    sizeof(out)));
  // 250 escaped data octets, three separators, and a NUL.
  EXPECT_EQ(255, LIBC_NAMESPACE::ns_name_uncompress(wire, wire + pos, wire, out,
                                                    1004));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_name_uncompress(wire, wire + pos, wire, out,
                                                   1003));
  EXPECT_EQ(255, LIBC_NAMESPACE::dn_skipname(wire, wire + pos));
  wire[192] = 62;
  wire[254] = 255;
  wire[255] = 0;
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_name_uncompress(wire, wire + 256, wire, out,
                                                   sizeof(out)));
  EXPECT_EQ(-1, LIBC_NAMESPACE::dn_skipname(wire, wire + 256));
  wire[0] = 64;
  EXPECT_EQ(-1, LIBC_NAMESPACE::dn_skipname(wire, wire + 256));
}

TEST(LlvmLibcDnsPacket, SectionsAndRandomSequentialRecords) {
  ns_msg msg;
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_initparse(RESPONSE, sizeof(RESPONSE), &msg));
  EXPECT_EQ(0x1234, ns_msg_id(msg));
  EXPECT_EQ(ns_msg_base(msg), RESPONSE);
  EXPECT_EQ(ns_msg_end(msg), RESPONSE + sizeof(RESPONSE));
  EXPECT_EQ(static_cast<size_t>(ns_msg_size(msg)), sizeof(RESPONSE));
  EXPECT_EQ(1, ns_msg_count(msg, ns_s_qd));
  EXPECT_EQ(2, ns_msg_count(msg, ns_s_an));
  EXPECT_EQ(1, ns_msg_count(msg, ns_s_ns));
  EXPECT_EQ(1, ns_msg_count(msg, ns_s_ar));
  EXPECT_EQ(0x8580, static_cast<int>(msg._flags));

  ns_rr rr;
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_an, 1, &rr));
  EXPECT_EQ(ns_t_mx, ns_rr_type(rr));
  EXPECT_EQ(10U, LIBC_NAMESPACE::ns_get16(ns_rr_rdata(rr)));
  char name[NS_MAXDNAME];
  EXPECT_EQ(2, LIBC_NAMESPACE::ns_name_uncompress(
                   ns_msg_base(msg), ns_msg_end(msg), ns_rr_rdata(rr) + 2, name,
                   sizeof(name)));
  EXPECT_STREQ("x.test", name);
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_an, -1, &rr));
  EXPECT_EQ(ENODEV, static_cast<int>(libc_errno));
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_an, 0, &rr));
  EXPECT_STREQ("x.test", ns_rr_name(rr));
  EXPECT_EQ(ns_t_a, ns_rr_type(rr));
  EXPECT_EQ(ns_c_in, ns_rr_class(rr));
  EXPECT_EQ(0xfedcba98UL, static_cast<unsigned long>(ns_rr_ttl(rr)));
  EXPECT_EQ(4, ns_rr_rdlen(rr));
  EXPECT_EQ(ns_rr_rdata(rr), RESPONSE + 36);
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_an, -1, &rr));
  EXPECT_EQ(ns_t_mx, ns_rr_type(rr));
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_qd, -1, &rr));
  EXPECT_EQ(ns_t_a, ns_rr_type(rr));
  EXPECT_EQ(0UL, static_cast<unsigned long>(ns_rr_ttl(rr)));
  EXPECT_EQ(0, ns_rr_rdlen(rr));
  EXPECT_EQ(ns_rr_rdata(rr), static_cast<const unsigned char *>(nullptr));
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_ns, -1, &rr));
  EXPECT_EQ(ns_t_ns, ns_rr_type(rr));
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_ar, -1, &rr));
  EXPECT_STREQ(".", ns_rr_name(rr));
  EXPECT_EQ(ns_t_opt, ns_rr_type(rr));
  EXPECT_EQ(0, ns_rr_rdlen(rr));
  EXPECT_EQ(ns_rr_rdata(rr), RESPONSE + sizeof(RESPONSE));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_max, 0, &rr));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_an, -2, &rr));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_an, 2, &rr));

  const unsigned char *end = RESPONSE + sizeof(RESPONSE);
  EXPECT_EQ(12, LIBC_NAMESPACE::ns_skiprr(RESPONSE + 12, end, ns_s_qd, 1));
  EXPECT_EQ(32, LIBC_NAMESPACE::ns_skiprr(RESPONSE + 24, end, ns_s_an, 2));
  EXPECT_EQ(14, LIBC_NAMESPACE::ns_skiprr(RESPONSE + 56, end, ns_s_ns, 1));
  EXPECT_EQ(11, LIBC_NAMESPACE::ns_skiprr(RESPONSE + 70, end, ns_s_ar, 1));
  EXPECT_EQ(0, LIBC_NAMESPACE::ns_skiprr(end, end, ns_s_ar, 0));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_skiprr(end, end, ns_s_ar, 1));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_skiprr(RESPONSE, end, ns_s_max, 0));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_skiprr(RESPONSE, end, ns_s_qd, -1));
}

TEST(LlvmLibcDnsPacket, PacketTruncationCountsAndBadPointers) {
  ns_msg msg = {};
  for (unsigned int size = 0; size < sizeof(RESPONSE); ++size) {
    EXPECT_EQ(-1, LIBC_NAMESPACE::ns_initparse(RESPONSE, size, &msg));
    EXPECT_EQ(EMSGSIZE, static_cast<int>(libc_errno));
  }
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_initparse(RESPONSE, -1, &msg));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_initparse(RESPONSE, NS_MAXMSG + 1, &msg));
  unsigned char packet[sizeof(RESPONSE) + 1] = {};
  for (unsigned int i = 0; i < sizeof(RESPONSE); ++i)
    packet[i] = RESPONSE[i];
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_initparse(packet, sizeof(packet), &msg));
  packet[25] = 24; // First answer points to itself.
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_initparse(packet, sizeof(RESPONSE), &msg));
  packet[25] = 12;
  packet[34] = 255; // RDLENGTH exceeds packet.
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_initparse(packet, sizeof(RESPONSE), &msg));
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_skiprr(
                    packet + 24, packet + sizeof(RESPONSE), ns_s_an, 1));
  packet[34] = 0;
  packet[11] = 2; // Missing additional record.
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_initparse(packet, sizeof(RESPONSE), &msg));
  packet[11] = 1;
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_initparse(packet, sizeof(RESPONSE), &msg));
  ns_rr rr = {};
  packet[25] = 24; // Revalidate mutable packet bytes during parsing too.
  EXPECT_EQ(-1, LIBC_NAMESPACE::ns_parserr(&msg, ns_s_an, 0, &rr));
}

TEST(LlvmLibcDnsPacket, EmptySections) {
  unsigned char packet[NS_HFIXEDSZ] = {};
  ns_msg msg;
  ns_rr rr;
  ASSERT_EQ(0, LIBC_NAMESPACE::ns_initparse(packet, sizeof(packet), &msg));
  for (int s = 0; s < ns_s_max; ++s) {
    EXPECT_EQ(0, ns_msg_count(msg, s));
    EXPECT_EQ(
        -1, LIBC_NAMESPACE::ns_parserr(&msg, static_cast<ns_sect>(s), -1, &rr));
  }
}

} // namespace LIBC_NAMESPACE_DECL
