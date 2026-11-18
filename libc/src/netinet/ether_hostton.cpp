// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/netinet/ether_hostton.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/netinet/ether_aton_r.h"
namespace LIBC_NAMESPACE_DECL {
namespace {
struct Reader {
  explicit Reader(int file) : fd(file) {}
  int fd, error = 0;
  unsigned char bytes[256];
  size_t at = 0, end = 0;
  int next() {
    if (at == end) {
      for (;;) {
        auto r = linux_syscalls::read(fd, bytes, sizeof(bytes));
        if (!r.has_value()) {
          if (r.error() == EINTR)
            continue;
          error = r.error();
          return -1;
        }
        if (!r.value())
          return -1;
        at = 0;
        end = static_cast<size_t>(r.value());
        break;
      }
    }
    return bytes[at++];
  }
};
bool space(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
} // namespace
LLVM_LIBC_FUNCTION(int, ether_hostton,
                   (const char *host, struct ether_addr *address)) {
  auto fd = linux_syscalls::open("/etc/ethers", O_RDONLY | O_CLOEXEC, 0);
  if (!fd.has_value()) {
    libc_errno = fd.error();
    return -1;
  }
  Reader reader{fd.value()};
  bool found = false;
  struct ether_addr parsed;
  int c = reader.next();
  while (c >= 0 && !found) {
    while (c != '\n' && space(c))
      c = reader.next();
    char mac[18];
    unsigned length = 0;
    while (c >= 0 && !space(c) && c != '#') {
      if (length < sizeof(mac) - 1)
        mac[length++] = static_cast<char>(c);
      else
        length = sizeof(mac);
      c = reader.next();
    }
    bool valid = length < sizeof(mac);
    if (valid) {
      mac[length] = 0;
      valid = LIBC_NAMESPACE::ether_aton_r(mac, &parsed);
    }
    while (c != '\n' && space(c))
      c = reader.next();
    const char *match = host;
    bool equal = valid && *host;
    while (c >= 0 && !space(c) && c != '#') {
      if (!*match || lower(static_cast<unsigned char>(*match)) != lower(c))
        equal = false;
      if (*match)
        ++match;
      c = reader.next();
    }
    found = equal && !*match;
    while (c >= 0 && c != '\n')
      c = reader.next();
    if (!found && c >= 0)
      c = reader.next();
  }
  (void)linux_syscalls::close(fd.value());
  if (reader.error) {
    libc_errno = reader.error;
    return -1;
  }
  if (!found) {
    libc_errno = ENOENT;
    return -1;
  }
  *address = parsed;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
