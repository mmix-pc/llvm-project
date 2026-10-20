//===-- Implementation of putgrent ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/putgrent.h"
#include "hdr/errno_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/char_vector.h"
#include "src/__support/common.h"
#include "src/__support/integer_to_string.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
bool valid_field(const char *field, bool member = false) {
  if (!field)
    return false;
  for (; *field; ++field)
    if (*field == ':' || *field == '\n' || (member && *field == ','))
      return false;
  return true;
}
} // namespace

LLVM_LIBC_FUNCTION(int, putgrent, (const struct group *entry, ::FILE *stream)) {
  if (!entry || !stream || !valid_field(entry->gr_name) || !entry->gr_name[0] ||
      !valid_field(entry->gr_passwd)) {
    libc_errno = EINVAL;
    return -1;
  }
  if (entry->gr_mem)
    for (char **member = entry->gr_mem; *member; ++member)
      if (!**member || !valid_field(*member, true)) {
        libc_errno = EINVAL;
        return -1;
      }
  CharVector line;
  auto append = [&](cpp::string_view text) {
    for (char ch : text)
      if (!line.append(ch))
        return false;
    return true;
  };
  IntegerToString<gid_t> gid(entry->gr_gid);
  bool ok = append(entry->gr_name) && line.append(':') &&
            append(entry->gr_passwd) && line.append(':') &&
            append(gid.view()) && line.append(':');
  if (entry->gr_mem)
    for (char **member = entry->gr_mem; ok && *member; ++member)
      ok = (member == entry->gr_mem || line.append(',')) && append(*member);
  if (!ok || !line.append('\n')) {
    libc_errno = ENOMEM;
    return -1;
  }
  auto written =
      reinterpret_cast<File *>(stream)->write(line.c_str(), line.length());
  if (written.has_error() || written.value != line.length()) {
    libc_errno = written.has_error() ? written.error : EIO;
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
