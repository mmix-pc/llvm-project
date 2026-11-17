//===-- Interface address enumeration tests
//--------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/__support/libc_errno.h"
#include "src/net/freeifaddrs.h"
#include "src/net/getifaddrs.h"
#include "src/net/linux/ifaddrs_impl.h"
#include "test/UnitTest/Test.h"
#include <stdlib.h>
#include <string.h>

using namespace LIBC_NAMESPACE;
namespace D = LIBC_NAMESPACE::net::ifaddrs_detail;

struct Memory {
  static inline int live = 0, remaining = -1;
  static void *allocate(size_t size) {
    if (remaining == 0)
      return nullptr;
    if (remaining > 0)
      --remaining;
    void *p = ::malloc(size);
    if (p)
      ++live;
    return p;
  }
  static void deallocate(void *p) {
    if (p) {
      --live;
      ::free(p);
    }
  }
};
struct Message {
  alignas(nlmsghdr) unsigned char bytes[1024] = {};
  nlmsghdr &header() { return *reinterpret_cast<nlmsghdr *>(bytes); }
  template <typename T> T &start(unsigned type, unsigned sequence) {
    header().nlmsg_len = NLMSG_LENGTH(sizeof(T));
    header().nlmsg_type = static_cast<unsigned short>(type);
    header().nlmsg_seq = sequence;
    return *::new (NLMSG_DATA(&header())) T{};
  }
  void attribute(unsigned type, const void *value, unsigned size) {
    auto *a = reinterpret_cast<rtattr *>(bytes + header().nlmsg_len);
    a->rta_type = static_cast<unsigned short>(type);
    a->rta_len = static_cast<unsigned short>(RTA_LENGTH(size));
    D::copy(RTA_DATA(a), value, size);
    header().nlmsg_len += RTA_SPACE(size);
  }
  ErrorOr<bool> parse(bool links, D::List<Memory> &list) {
    return D::parse(bytes, header().nlmsg_len, links ? 1 : 2, links, list);
  }
};

TEST(LlvmLibcIfAddrsTest, LinkAndAddressMetadata) {
  Memory::remaining = -1;
  {
    D::List<Memory> list;
    Message link;
    auto &info = link.start<ifinfomsg>(RTM_NEWLINK, 1);
    info.ifi_index = 7;
    info.ifi_type = 1;
    info.ifi_flags = IFF_UP | IFF_BROADCAST;
    const char name[] = "test0";
    const unsigned char mac[] = {2, 1, 2, 3, 4, 5};
    link.attribute(IFLA_IFNAME, name, sizeof(name));
    link.attribute(IFLA_ADDRESS, mac, sizeof(mac));
    rtnl_link_stats stats{};
    stats.rx_packets = 41;
    link.attribute(IFLA_STATS, &stats, sizeof(stats));
    ASSERT_TRUE(link.parse(true, list).has_value());
    ASSERT_STREQ(list.head->ifa_name, "test0");
    auto *packet = reinterpret_cast<sockaddr_ll *>(list.head->ifa_addr);
    ASSERT_EQ(packet->sll_ifindex, 7);
    ASSERT_EQ(packet->sll_halen, static_cast<unsigned char>(6));
    ASSERT_EQ(::memcmp(packet->sll_addr, mac, sizeof(mac)), 0);
    ASSERT_EQ(static_cast<rtnl_link_stats *>(list.head->ifa_data)->rx_packets,
              41U);

    Message v4;
    auto &address = v4.start<ifaddrmsg>(RTM_NEWADDR, 2);
    address.ifa_family = AF_INET;
    address.ifa_index = 7;
    address.ifa_prefixlen = 24;
    const unsigned char local[] = {192, 0, 2, 1},
                        broadcast[] = {192, 0, 2, 255};
    v4.attribute(IFA_LOCAL, local, 4);
    v4.attribute(IFA_BROADCAST, broadcast, 4);
    ASSERT_TRUE(v4.parse(false, list).has_value());
    auto *row = list.head->ifa_next;
    ASSERT_EQ(
        ::memcmp(&reinterpret_cast<sockaddr_in *>(row->ifa_addr)->sin_addr,
                 local, 4),
        0);
    ASSERT_EQ(
        ::memcmp(&reinterpret_cast<sockaddr_in *>(row->ifa_broadaddr)->sin_addr,
                 broadcast, 4),
        0);
    const unsigned char mask[] = {255, 255, 255, 0};
    ASSERT_EQ(
        ::memcmp(&reinterpret_cast<sockaddr_in *>(row->ifa_netmask)->sin_addr,
                 mask, 4),
        0);
    ASSERT_EQ(row->ifa_flags, static_cast<unsigned>(IFF_UP | IFF_BROADCAST));

    Message v6;
    auto &ipv6 = v6.start<ifaddrmsg>(RTM_NEWADDR, 2);
    ipv6.ifa_family = AF_INET6;
    ipv6.ifa_index = 7;
    ipv6.ifa_prefixlen = 64;
    unsigned char local6[16] = {0xfe, 0x80};
    local6[15] = 1;
    v6.attribute(IFA_ADDRESS, local6, 16);
    ASSERT_TRUE(v6.parse(false, list).has_value());
    row = row->ifa_next;
    ASSERT_EQ(reinterpret_cast<sockaddr_in6 *>(row->ifa_addr)->sin6_scope_id,
              7U);
    ASSERT_EQ(
        ::memcmp(&reinterpret_cast<sockaddr_in6 *>(row->ifa_addr)->sin6_addr,
                 local6, 16),
        0);
    const auto *mask6 = reinterpret_cast<const unsigned char *>(
        &reinterpret_cast<sockaddr_in6 *>(row->ifa_netmask)->sin6_addr);
    for (unsigned i = 0; i < 16; ++i)
      EXPECT_EQ(mask6[i], static_cast<unsigned char>(i < 8 ? 255 : 0));
  }
  EXPECT_EQ(Memory::live, 0);
}

TEST(LlvmLibcIfAddrsTest, RejectMalformedOrInterruptedMessages) {
  D::List<Memory> list;
  Message message;
  message.start<int>(NLMSG_DONE, 1);
  message.header().nlmsg_flags = NLM_F_DUMP_INTR;
  EXPECT_EQ(message.parse(true, list).error(), EAGAIN);
  message.header().nlmsg_flags = 0;
  message.header().nlmsg_seq = 77;
  EXPECT_EQ(message.parse(true, list).error(), EINVAL);
  message.header().nlmsg_seq = 1;
  message.header().nlmsg_len = 3;
  EXPECT_EQ(message.parse(true, list).error(), EINVAL);
  message = {};
  auto &link = message.start<ifinfomsg>(RTM_NEWLINK, 1);
  link.ifi_index = 1;
  const char name[] = "test0";
  message.attribute(IFLA_IFNAME, name, sizeof(name));
  auto *attribute = reinterpret_cast<rtattr *>(message.bytes +
                                               NLMSG_LENGTH(sizeof(ifinfomsg)));
  attribute->rta_len = 1;
  EXPECT_EQ(message.parse(true, list).error(), EINVAL);
  Message address;
  auto &info = address.start<ifaddrmsg>(RTM_NEWADDR, 2);
  info.ifa_family = AF_INET;
  info.ifa_index = 9;
  info.ifa_prefixlen = 33;
  EXPECT_EQ(address.parse(false, list).error(), EINVAL);
  info.ifa_prefixlen = 24;
  const unsigned char ip[4] = {192, 0, 2, 1};
  address.attribute(IFA_LOCAL, ip, 4);
  EXPECT_EQ(address.parse(false, list).error(), EAGAIN);
  EXPECT_EQ(Memory::live, 0);
}

struct Transport : Memory {
  static inline int mode = 0, closes = 0, sends = 0, receives = 0;
  static inline unsigned sequence = 0;
  static ErrorOr<int> socket(int, int, int) {
    return mode == 1 ? ErrorOr<int>(Error(EACCES)) : ErrorOr<int>(19);
  }
  static ErrorOr<int> close(int) {
    ++closes;
    return mode == 5 ? ErrorOr<int>(Error(EINTR)) : ErrorOr<int>(0);
  }
  static ErrorOr<ssize_t> sendto(int, const void *data, size_t size, int,
                                 const sockaddr *, socklen_t) {
    if (mode == 6 && !sends++)
      return Error(EINTR);
    sequence = static_cast<const nlmsghdr *>(data)->nlmsg_seq;
    return static_cast<ssize_t>(mode == 2 ? size - 1 : size);
  }
  static ErrorOr<ssize_t> recvfrom(int, void *data, size_t, int flags,
                                   sockaddr *sender, socklen_t *length) {
    if (mode == 6 && !receives++)
      return Error(EINTR);
    if (flags & MSG_PEEK)
      return static_cast<ssize_t>(NLMSG_LENGTH(sizeof(int)));
    Message message;
    message.start<int>(NLMSG_DONE, sequence);
    D::copy(data, message.bytes, message.header().nlmsg_len);
    auto *peer = reinterpret_cast<sockaddr_nl *>(sender);
    peer->nl_family = AF_NETLINK;
    peer->nl_pid = mode == 3 ? 7 : 0;
    *length = sizeof(*peer);
    return static_cast<ssize_t>(mode == 4 ? 1 : message.header().nlmsg_len);
  }
};

TEST(LlvmLibcIfAddrsTest, TransportErrorsAndCleanup) {
  for (int mode = 0; mode <= 6; ++mode) {
    Transport::mode = mode;
    Transport::closes = Transport::sends = Transport::receives = 0;
    Memory::remaining = -1;
    auto result = D::enumerate<Transport>();
    EXPECT_EQ(result.has_value(), mode == 0 || mode == 6);
    EXPECT_EQ(Transport::closes, mode == 1 ? 0 : 1);
    EXPECT_EQ(Memory::live, 0);
  }
  Transport::mode = 0;
  Transport::closes = 0;
  Memory::remaining = 0;
  EXPECT_EQ(D::enumerate<Transport>().error(), ENOMEM);
  EXPECT_EQ(Transport::closes, 1);
  EXPECT_EQ(Memory::live, 0);
  Memory::remaining = -1;
}

TEST(LlvmLibcIfAddrsTest, RealInterfacesAndPublicCleanup) {
  ifaddrs *list = nullptr;
  libc_errno = EDOM;
  int result = LIBC_NAMESPACE::getifaddrs(&list);
  EXPECT_EQ(static_cast<int>(libc_errno), EDOM);
  ASSERT_EQ(result, 0);
  EXPECT_EQ(static_cast<int>(libc_errno), EDOM);
  bool loopback = false;
  for (ifaddrs *entry = list; entry; entry = entry->ifa_next) {
    ASSERT_NE(entry->ifa_name, static_cast<char *>(nullptr));
    if (::strcmp(entry->ifa_name, "lo") == 0)
      loopback = true;
  }
  EXPECT_TRUE(loopback);
  LIBC_NAMESPACE::freeifaddrs(list);
  LIBC_NAMESPACE::freeifaddrs(nullptr);
  EXPECT_EQ(static_cast<int>(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::getifaddrs(nullptr), -1);
  EXPECT_EQ(static_cast<int>(libc_errno), EFAULT);
}
