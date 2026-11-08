//===-- Tests for ancillary message macros --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <sys/socket.h>

#ifdef __cplusplus
#define CHECK_CONSTANT static_assert
#else
#define CHECK_CONSTANT _Static_assert
#endif
CHECK_CONSTANT(CMSG_LEN(0) == sizeof(struct cmsghdr), "header length");
CHECK_CONSTANT(CMSG_LEN(3) == sizeof(struct cmsghdr) + 3, "payload length");
CHECK_CONSTANT(CMSG_SPACE(1) == sizeof(struct cmsghdr) + sizeof(size_t),
               "payload alignment");
#undef CHECK_CONSTANT

static union {
  struct cmsghdr align;
  unsigned char bytes[128];
} control;
static struct msghdr message;
static unsigned message_calls, header_calls, length_calls;
static struct cmsghdr *selected_header;

static struct msghdr *next_message(void) {
  ++message_calls;
  return &message;
}
static struct cmsghdr *next_header(void) {
  ++header_calls;
  return selected_header;
}
static size_t next_length(void) {
  ++length_calls;
  return 1;
}

#ifdef __cplusplus
// These must be ordinary expression contexts, not statement expressions.
static struct cmsghdr *initial_first = CMSG_FIRSTHDR(&message);
static struct cmsghdr *initial_next = CMSG_NXTHDR(&message, nullptr);
#endif

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (0)

static int check_cmsg_macros(void) {
#ifdef __cplusplus
  CHECK(initial_first == 0 && initial_next == 0);
#endif
  message.msg_control = control.bytes;
  struct cmsghdr *first = (struct cmsghdr *)control.bytes;
  selected_header = first;
  first->cmsg_len = CMSG_LEN(1);
  struct cmsghdr *second = (struct cmsghdr *)(control.bytes + CMSG_SPACE(1));
  second->cmsg_len = CMSG_LEN(1);

  for (size_t size = 0; size < sizeof(struct cmsghdr); ++size) {
    message.msg_controllen = size;
    message_calls = header_calls = 0;
    CHECK(CMSG_FIRSTHDR(next_message()) == 0 && message_calls == 1);
    selected_header = 0;
    message_calls = 0;
    CHECK(CMSG_NXTHDR(next_message(), next_header()) == 0);
    CHECK(message_calls == 1 && header_calls == 1);
  }
  message.msg_controllen = sizeof(control.bytes);
  selected_header = 0;
  message_calls = header_calls = 0;
  CHECK(CMSG_NXTHDR(next_message(), next_header()) == first);
  CHECK(message_calls == 1 && header_calls == 1);
  message_calls = 0;
  CHECK(CMSG_FIRSTHDR(next_message()) == first && message_calls == 1);
  selected_header = first;
  message_calls = header_calls = 0;
  CHECK(CMSG_NXTHDR(next_message(), next_header()) == second);
  CHECK(message_calls == 1 && header_calls == 1);
  header_calls = 0;
  CHECK(CMSG_DATA(next_header()) == control.bytes + sizeof(struct cmsghdr));
  CHECK(header_calls == 1);
  length_calls = 0;
  CHECK(CMSG_LEN(next_length()) == CMSG_LEN(1) && length_calls == 1);
  length_calls = 0;
  CHECK(CMSG_SPACE(next_length()) == CMSG_SPACE(1) && length_calls == 1);

  // Every padding boundary and insufficient room for the next complete header.
  for (size_t payload = 0; payload <= 2 * sizeof(size_t); ++payload) {
    first->cmsg_len = CMSG_LEN(payload);
    size_t offset = CMSG_SPACE(payload);
    for (size_t tail = 0; tail < sizeof(struct cmsghdr); ++tail) {
      message.msg_controllen = offset + tail;
      CHECK(CMSG_NXTHDR(&message, first) == 0);
    }
    second = (struct cmsghdr *)(control.bytes + offset);
    second->cmsg_len = CMSG_LEN(1);
    message.msg_controllen = offset + sizeof(struct cmsghdr);
    // The next header is complete but its payload is truncated: either result
    // is permitted by the public contract.
    struct cmsghdr *next = CMSG_NXTHDR(&message, first);
    CHECK(next == 0 || next == second);
    message.msg_controllen = offset + CMSG_SPACE(1);
    CHECK(CMSG_NXTHDR(&message, first) == second);
    CHECK(CMSG_NXTHDR(&message, second) == 0);
  }
  // Malformed lengths must not wrap pointer arithmetic or loop on the header.
  first->cmsg_len = 0;
  CHECK(CMSG_NXTHDR(&message, first) == 0);
  first->cmsg_len = (size_t)-1;
  CHECK(CMSG_NXTHDR(&message, first) == 0);
  return 0;
}
#undef CHECK
