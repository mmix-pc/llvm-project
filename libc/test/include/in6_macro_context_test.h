//===-- IPv6 classification macro context and evaluation tests
//-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <netinet/in.h>

#define FOR_EACH_IN6(X)                                                        \
  X(0, IN6_IS_ADDR_UNSPECIFIED)                                                \
  X(1, IN6_IS_ADDR_LOOPBACK)                                                   \
  X(2, IN6_IS_ADDR_MULTICAST)                                                  \
  X(3, IN6_IS_ADDR_LINKLOCAL)                                                  \
  X(4, IN6_IS_ADDR_SITELOCAL)                                                  \
  X(5, IN6_IS_ADDR_V4MAPPED)                                                   \
  X(6, IN6_IS_ADDR_V4COMPAT)                                                   \
  X(7, IN6_IS_ADDR_MC_NODELOCAL)                                               \
  X(8, IN6_IS_ADDR_MC_LINKLOCAL)                                               \
  X(9, IN6_IS_ADDR_MC_SITELOCAL)                                               \
  X(10, IN6_IS_ADDR_MC_ORGLOCAL)                                               \
  X(11, IN6_IS_ADDR_MC_GLOBAL)

// This declaration has no definition: unevaluated contexts must not call it.
extern const struct in6_addr *context_address(void);
#ifdef __cplusplus
#define CHECK_CONTEXT(index, name)                                             \
  static_assert(sizeof(name(context_address())) == sizeof(int));               \
  static_assert(__is_same(decltype(name(context_address())), int));
#else
#define CHECK_CONTEXT(index, name)                                             \
  _Static_assert(sizeof(name(context_address())) == sizeof(int), #name);       \
  _Static_assert(_Generic(name(context_address()), int: 1, default: 0), #name);
#endif
FOR_EACH_IN6(CHECK_CONTEXT)
#undef CHECK_CONTEXT

#ifdef __cplusplus
static const struct in6_addr initial_loopback = IN6ADDR_LOOPBACK_INIT;
#define CHECK_INITIALIZER(index, name)                                         \
  static int initial_##index = name(&initial_loopback);
FOR_EACH_IN6(CHECK_INITIALIZER)
#undef CHECK_INITIALIZER
#endif

static unsigned calls;
static const struct in6_addr *selected_address;
static const struct in6_addr *next_address(void) {
  ++calls;
  return selected_address;
}

static int matches(const struct in6_addr *address, unsigned expected) {
  selected_address = address;
  unsigned actual = 0;
#define CHECK_VALUE(index, name)                                               \
  do {                                                                         \
    calls = 0;                                                                 \
    int result = name(next_address() + 0);                                     \
    if (calls != 1)                                                            \
      return 0;                                                                \
    actual |= (result != 0) << index;                                          \
  } while (0);
  FOR_EACH_IN6(CHECK_VALUE)
#undef CHECK_VALUE
  return actual == expected;
}

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (0)

static int check_in6_macros(void) {
#ifdef __cplusplus
#define CHECK_INITIAL_VALUE(index, name)                                       \
  CHECK((initial_##index != 0) == (index == 1));
  FOR_EACH_IN6(CHECK_INITIAL_VALUE)
#undef CHECK_INITIAL_VALUE
#endif
  struct in6_addr address = IN6ADDR_ANY_INIT;
  CHECK(matches(&address, 1U << 0));
  address.s6_addr[15] = 1;
  CHECK(matches(&address, 1U << 1));
  address.s6_addr[15] = 2;
  CHECK(matches(&address, 1U << 6));
  address.s6_addr[15] = 0;

  // Changing every individual zero byte separates compatible, loopback and
  // ordinary addresses without relying on the representation of word aliases.
  for (unsigned i = 0; i < 16; ++i) {
    address.s6_addr[i] = 1;
    CHECK(matches(&address, i < 12 ? 0 : i == 15 ? 1U << 1 : 1U << 6));
    address.s6_addr[i] = 0;
  }
  address.s6_addr[10] = address.s6_addr[11] = 0xff;
  address.s6_addr[12] = 192;
  address.s6_addr[14] = 2;
  address.s6_addr[15] = 1;
  CHECK(matches(&address, 1U << 5));
  for (unsigned i = 0; i < 10; ++i) {
    address.s6_addr[i] = 1;
    CHECK(matches(&address, 0));
    address.s6_addr[i] = 0;
  }
  address.s6_addr[10] = address.s6_addr[11] = 0;
  CHECK(matches(&address, 1U << 6));
  address.s6_addr[12] = address.s6_addr[14] = address.s6_addr[15] = 0;

  // All second-byte values cover scope flag bits and both unicast prefixes.
  for (unsigned second = 0; second < 256; ++second) {
    address.s6_addr[0] = 0xff;
    address.s6_addr[1] = (unsigned char)second;
    unsigned mask = 1U << 2;
    switch (second & 15) {
    case 1:
      mask |= 1U << 7;
      break;
    case 2:
      mask |= 1U << 8;
      break;
    case 5:
      mask |= 1U << 9;
      break;
    case 8:
      mask |= 1U << 10;
      break;
    case 14:
      mask |= 1U << 11;
      break;
    }
    CHECK(matches(&address, mask));
    address.s6_addr[0] = 0xfe;
    mask = second >= 192 ? 1U << 4 : second >= 128 ? 1U << 3 : 0;
    CHECK(matches(&address, mask));
    address.s6_addr[0] = 0xfd;
    CHECK(matches(&address, 0));
  }
  return 0;
}
#undef CHECK
#undef FOR_EACH_IN6
