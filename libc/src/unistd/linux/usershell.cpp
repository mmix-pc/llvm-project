// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/unistd/linux/usershell.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_mman_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mmap.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/munmap.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/libc_errno.h"
namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
// Legacy enumeration shares its snapshot and cursor, like getusershell itself.
char defaults[] = "/bin/sh\0/bin/csh";
char *storage;
size_t capacity, used, cursor;
bool initialized;
void fallback() {
  if (storage)
    (void)linux_syscalls::munmap(storage, capacity);
  storage = nullptr;
  capacity = 0;
  used = sizeof(defaults);
  cursor = 0;
}
bool grow() {
  if (capacity > static_cast<size_t>(-1) / 2) {
    libc_errno = ENOMEM;
    return false;
  }
  size_t next = capacity ? capacity * 2 : 4096;
  auto memory = linux_syscalls::mmap(nullptr, next, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!memory.has_value()) {
    libc_errno = memory.error();
    return false;
  }
  char *replacement = static_cast<char *>(memory.value());
  for (size_t i = 0; i < used; ++i)
    replacement[i] = storage[i];
  if (storage)
    (void)linux_syscalls::munmap(storage, capacity);
  storage = replacement;
  capacity = next;
  return true;
}
bool space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
void load() {
  initialized = true;
  used = cursor = 0;
  auto file = linux_syscalls::open("/etc/shells", O_RDONLY | O_CLOEXEC, 0);
  if (!file.has_value()) {
    libc_errno = file.error();
    fallback();
    return;
  }
  bool ok = true;
  for (;;) {
    if (capacity <= used + 1 && !grow()) {
      ok = false;
      break;
    }
    auto count =
        linux_syscalls::read(file.value(), storage + used, capacity - used - 1);
    if (!count.has_value()) {
      if (count.error() == EINTR)
        continue;
      libc_errno = count.error();
      ok = false;
      break;
    }
    if (!count.value())
      break;
    used += static_cast<size_t>(count.value());
  }
  (void)linux_syscalls::close(file.value());
  if (!ok) {
    fallback();
    return;
  }
  size_t from = 0, to = 0;
  while (from < used) {
    size_t end = from;
    while (end < used && storage[end] != '\n')
      ++end;
    size_t start = from;
    while (start < end && storage[start] != '/' && storage[start] != '#')
      ++start;
    if (start < end && storage[start] == '/') {
      while (start < end && storage[start] != '#' && !space(storage[start]) &&
             storage[start])
        storage[to++] = storage[start++];
      storage[to++] = 0;
    }
    from = end + 1;
  }
  used = to;
  if (!used)
    fallback();
}
} // namespace
char *next_shell() {
  if (!initialized)
    load();
  char *data = storage ? storage : defaults;
  while (cursor < used && !data[cursor])
    ++cursor;
  if (cursor == used)
    return nullptr;
  char *result = data + cursor;
  while (cursor < used && data[cursor])
    ++cursor;
  return result;
}
void reset_shells() {
  if (!initialized)
    load();
  cursor = 0;
}
void end_shells() {
  if (storage)
    (void)linux_syscalls::munmap(storage, capacity);
  storage = nullptr;
  capacity = used = cursor = 0;
  initialized = false;
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
