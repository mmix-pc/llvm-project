//===-- POSIX implementation of strerror_r --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/string/__xpg_strerror_r.h"
#include "hdr/errno_macros.h"
#include "src/__support/StringUtil/error_to_string.h"
#include "src/__support/common.h"
#include "src/__support/integer_to_string.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, __xpg_strerror_r,
                   (int err_num, char *buf, size_t buflen)) {
  auto known = try_get_error_string(err_num);
  // Unknown messages use call-local storage, never strerror's TLS buffer.
  char scratch[sizeof("Unknown error") + 1 +
               IntegerToString<int>::buffer_size()];
  auto message = known ? *known : get_error_string(err_num, scratch);
  size_t length = message.size();
  if (length && message[length - 1] == '\0')
    --length;
  if (buflen) {
    size_t count = length < buflen - 1 ? length : buflen - 1;
    for (size_t i = 0; i < count; ++i)
      buf[i] = message[i];
    buf[count] = '\0';
  }
  if (buflen <= length)
    return ERANGE;
  return known ? 0 : EINVAL;
}

} // namespace LIBC_NAMESPACE_DECL
