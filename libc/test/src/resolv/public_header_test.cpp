//===-- DNS public types and macros, including in overlay tests -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Deliberately do not use proxies: verify our wire layout and macros even
// when the other test translation units use host headers in an overlay build.
#include "include/llvm-libc-macros/resolv-macros.h"
#include "include/llvm-libc-types/ns_flag.h"
#include "include/llvm-libc-types/ns_header.h"
#include "include/llvm-libc-types/ns_msg.h"
#include "include/llvm-libc-types/ns_opcode.h"
#include "include/llvm-libc-types/ns_rr.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {

TEST(LlvmLibcDnsPublicHeader, WireLayoutAndFlags) {
  static_assert(sizeof(HEADER) == NS_HFIXEDSZ);
  static_assert(QUERY == ns_o_query && C_IN == ns_c_in && T_A == ns_t_a);
  static_assert(T_PTR == ns_t_ptr && T_AAAA == ns_t_aaaa);
  static_assert(MAXDNAME == NS_MAXDNAME);
  HEADER header = {};
  unsigned char *wire = reinterpret_cast<unsigned char *>(&header);
  wire[2] = 0x85;
  wire[3] = 0x80;
  EXPECT_EQ(1U, static_cast<unsigned int>(header.aa));
  EXPECT_EQ(1U, static_cast<unsigned int>(header.qr));
  EXPECT_EQ(1U, static_cast<unsigned int>(header.rd));
  EXPECT_EQ(1U, static_cast<unsigned int>(header.ra));
  EXPECT_EQ(0U, static_cast<unsigned int>(header.opcode));
  EXPECT_EQ(0U, static_cast<unsigned int>(header.rcode));
  header.aa = 0;
  header.tc = 1;
  header.rcode = 3;
  EXPECT_EQ(0x83, static_cast<int>(wire[2]));
  EXPECT_EQ(0x83, static_cast<int>(wire[3]));
  header.qdcount = 0;
  wire[4] = 0x12;
  wire[5] = 0x34;
  const unsigned char *p = wire + 4;
  unsigned int value;
  NS_GET16(value, p);
  EXPECT_EQ(0x1234U, value);
  EXPECT_EQ(p, static_cast<const unsigned char *>(wire + 6));
  ns_msg msg = {};
  const unsigned int masks[] = {0x8000, 0x7800, 0x0400, 0x0200, 0x0100,
                                0x0080, 0x0040, 0x0020, 0x0010, 0x000f};
  for (int flag = 0; flag < ns_f_max; ++flag) {
    msg._flags = static_cast<__UINT16_TYPE__>(masks[flag]);
    for (int other = 0; other < ns_f_max; ++other) {
      unsigned int expected = flag != other                                 ? 0
                              : (flag == ns_f_opcode || flag == ns_f_rcode) ? 15
                                                                            : 1;
      EXPECT_EQ(expected, ns_msg_getflag(msg, other));
    }
  }
  EXPECT_EQ(0U, ns_msg_getflag(msg, -1));
  EXPECT_EQ(0U, ns_msg_getflag(msg, ns_f_max));
  ns_rr rr = {};
  EXPECT_STREQ(".", ns_rr_name(rr));
  rr.type = T_MX;
  rr.rr_class = C_IN;
  EXPECT_EQ(ns_t_mx, ns_rr_type(rr));
  EXPECT_EQ(ns_c_in, ns_rr_class(rr));
}

} // namespace LIBC_NAMESPACE_DECL
