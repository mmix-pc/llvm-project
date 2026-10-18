//===-- Supplementary group collection ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_GRP_SUPPLEMENTARY_GROUPS_H
#define LLVM_LIBC_SRC_GRP_SUPPLEMENTARY_GROUPS_H

#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "src/__support/libc_errno.h"
#include "src/grp/lookup.h"
#include <linux/limits.h>

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct SupplementaryGroups {
  gid_t *data = nullptr;
  size_t size = 0;
  size_t capacity = 0;

  SupplementaryGroups() = default;
  SupplementaryGroups(const SupplementaryGroups &) = delete;
  SupplementaryGroups &operator=(const SupplementaryGroups &) = delete;
  ~SupplementaryGroups() { ::free(data); }

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

  // Collect the entire database before allowing the caller to change credentials.
  int collect(const char *user, gid_t group) {
    if (!user || !*user || group == static_cast<gid_t>(-1))
      return EINVAL;
    struct RecordBuffer {
      AccountBuffer<struct group> value;
      ~RecordBuffer() { ::free(value.buffer); }
    } record;
    int saved_errno = libc_errno;
    libc_errno = 0;
    record.value.lookup([&](struct group *entry, char *buffer, size_t length,
                            struct group **result) {
      // A larger record buffer restarts the scan; discard partial membership.
      size = 0;
      int error = append(group);
      if (error)
        return error;
      int scan_error = group_db::lookup_group(
          [&](const struct group &candidate) {
            for (char **member = candidate.gr_mem; *member; ++member) {
              if (cpp::string_view(*member) == user) {
                error = append(candidate.gr_gid);
                break;
              }
            }
            return error != 0;
          },
          entry, buffer, length, result);
      return error ? error : scan_error;
    });
    int error = libc_errno;
    libc_errno = saved_errno;
    return error;
  }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
