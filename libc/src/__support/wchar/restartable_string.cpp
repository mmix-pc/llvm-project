//===-- Restartable locale string conversion -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/wchar/mbsnrtowcs.h"
#include "src/__support/wchar/restartable_converter.h"
#include "src/__support/wchar/wcsnrtombs.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<size_t> mbsnrtowcs(wchar_t *__restrict dst, const char **__restrict src,
                           size_t max_src_bytes, size_t max_dst_chars,
                           mbstate *__restrict state) {
  LIBC_CRASH_ON_NULLPTR(src);
  LIBC_CRASH_ON_NULLPTR(state);
  auto converter = RestartableConverter::current();
  if (!converter.valid(*state))
    return Error(EINVAL);
  mbstate sizing_state = *state;
  if (!dst) {
    state = &sizing_state;
    max_dst_chars = SIZE_MAX;
  }
  const char *input = *src;
  size_t consumed = 0, written = 0;
  while (consumed < max_src_bytes && written < max_dst_chars) {
    uint32_t scalar = 0;
    auto result =
        converter.decode(&scalar,
                         {reinterpret_cast<const uint8_t *>(input + consumed),
                          max_src_bytes - consumed},
                         *state);
    if (!result) {
      if (dst)
        *src = input + consumed;
      return Error(result.error());
    }
    if (*result == size_t(-2)) {
      consumed = max_src_bytes;
      break;
    }
    if (dst)
      dst[written] = static_cast<wchar_t>(scalar);
    if (scalar == 0) {
      if (dst)
        *src = nullptr;
      return written;
    }
    consumed += *result;
    ++written;
  }
  if (dst)
    *src = input + consumed;
  return written;
}

ErrorOr<size_t> wcsnrtombs(char *__restrict dst, const wchar_t **__restrict src,
                           size_t max_src_chars, size_t max_dst_bytes,
                           mbstate *__restrict state) {
  LIBC_CRASH_ON_NULLPTR(src);
  LIBC_CRASH_ON_NULLPTR(state);
  auto converter = RestartableConverter::current();
  if (!converter.valid(*state))
    return Error(EINVAL);
  mbstate sizing_state = *state;
  if (!dst) {
    state = &sizing_state;
    max_dst_bytes = SIZE_MAX;
  }
  const wchar_t *input = *src;
  size_t consumed = 0, written = 0;
  while (consumed < max_src_chars && written < max_dst_bytes) {
    // Preflight the entire character, including designation/reset bytes.
    mbstate candidate = *state;
    uint8_t bytes[MAX_LOCALE_CHARACTER_BYTES];
    auto result = converter.encode(static_cast<uint32_t>(input[consumed]),
                                   bytes, candidate);
    if (!result) {
      if (dst)
        *src = input + consumed;
      return Error(result.error());
    }
    if (*result > max_dst_bytes - written) {
      if (!dst)
        return Error(EOVERFLOW);
      break;
    }
    if (dst)
      for (size_t i = 0; i != *result; ++i)
        dst[written + i] = static_cast<char>(bytes[i]);
    written += *result;
    *state = candidate;
    if (input[consumed] == 0) {
      if (dst)
        *src = nullptr;
      return written - 1;
    }
    ++consumed;
  }
  if (dst)
    *src = input + consumed;
  return written;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
