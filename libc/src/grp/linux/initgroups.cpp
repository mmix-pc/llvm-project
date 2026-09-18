//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/grp/initgroups.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/setgroups.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/grp/lookup.h"
#include <linux/limits.h>

namespace LIBC_NAMESPACE_DECL {
namespace {
struct GroupList {
  gid_t *data = nullptr;
  size_t size = 0;
  size_t capacity = 0;

  ~GroupList() { ::free(data); }

  int append(gid_t gid) {
    if (gid == static_cast<gid_t>(-1))
      return EINVAL;
    for (size_t i = 0; i < size; ++i)
      if (data[i] == gid)
        return 0;
    if (size == NGROUPS_MAX)
      return EINVAL;
    if (size == capacity) {
      size_t next = capacity ? capacity * 2 : 16;
      if (next > NGROUPS_MAX)
        next = NGROUPS_MAX;
      auto *allocation =
          static_cast<gid_t *>(::realloc(data, next * sizeof(gid_t)));
      if (!allocation)
        return ENOMEM;
      data = allocation;
      capacity = next;
    }
    data[size++] = gid;
    return 0;
  }
};
struct RecordBuffer {
  internal::AccountBuffer<struct group> value;
  ~RecordBuffer() { ::free(value.buffer); }
};
} // namespace

LLVM_LIBC_FUNCTION(int, initgroups, (const char *user, gid_t group)) {
  if (!user || !*user || group == static_cast<gid_t>(-1)) {
    libc_errno = EINVAL;
    return -1;
  }
  int saved_errno = libc_errno;
  GroupList groups;
  RecordBuffer record;
  libc_errno = 0;
  record.value.lookup([&](struct group *entry, char *buffer, size_t size,
                          struct group **result) {
    // A larger record buffer restarts the scan; discard partial membership.
    groups.size = 0;
    int error = groups.append(group);
    if (error)
      return error;
    int scan_error = group_db::lookup_group(
        [&](const struct group &candidate) {
          for (char **member = candidate.gr_mem; *member; ++member) {
            if (cpp::string_view(*member) == user) {
              error = groups.append(candidate.gr_gid);
              break;
            }
          }
          return error != 0;
        },
        entry, buffer, size, result);
    return error ? error : scan_error;
  });
  if (libc_errno)
    return -1;

  // Do not change credentials after a partial scan or database/allocation
  // error.
  auto result = linux_syscalls::setgroups(groups.size, groups.data);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  libc_errno = saved_errno;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
