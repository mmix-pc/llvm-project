//===-- MMIX bare-metal wide stream I/O -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/fgetwc.h"
#include "src/wchar/fgetws.h"
#include "src/wchar/fputwc.h"
#include "src/wchar/fputws.h"
#include "src/wchar/fwide.h"
#include "src/wchar/getwc.h"
#include "src/wchar/getwchar.h"
#include "src/wchar/putwc.h"
#include "src/wchar/putwchar.h"
#include "src/wchar/ungetwc.h"

#include "hdr/errno_macros.h"
#include "hdr/types/ssize_t.h"
#include "hdr/wchar_macros.h"
#include "src/__support/OSUtil/baremetal/mmix/platform_io.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/wchar/mbrtowc.h"
#include "src/__support/wchar/mbstate.h"
#include "src/__support/wchar/wcrtomb.h"

namespace LIBC_NAMESPACE_DECL {
namespace {

LIBC_INLINE __llvm_libc_stdio_cookie *cookie(::FILE *stream) {
  return reinterpret_cast<__llvm_libc_stdio_cookie *>(stream);
}

LIBC_INLINE ::FILE *standard_input() {
  return reinterpret_cast<::FILE *>(&__llvm_libc_stdin_cookie);
}

LIBC_INLINE ::FILE *standard_output() {
  return reinterpret_cast<::FILE *>(&__llvm_libc_stdout_cookie);
}

LIBC_INLINE bool orient_wide(::FILE *stream) {
  return __llvm_libc_mmix_stream_orient(cookie(stream), 1) > 0;
}

wint_t write_wide_character(wchar_t wc, ::FILE *stream) {
  if (!orient_wide(stream)) {
    libc_errno = EINVAL;
    return WEOF;
  }

  char bytes[internal::MAX_LOCALE_CHARACTER_BYTES];
  internal::mbstate state{};
  auto encoded = internal::wcrtomb(bytes, wc, &state);
  if (!encoded.has_value()) {
    libc_errno = encoded.error();
    __llvm_libc_mmix_stream_set_error(cookie(stream));
    return WEOF;
  }
  if (__llvm_libc_stdio_write(cookie(stream), bytes, encoded.value()) !=
      static_cast<ssize_t>(encoded.value()))
    return WEOF;
  return static_cast<wint_t>(wc);
}

wint_t read_wide_character(::FILE *stream) {
  if (!orient_wide(stream)) {
    libc_errno = EINVAL;
    return WEOF;
  }

  internal::mbstate state{};
  wchar_t wc = L'\0';
  for (unsigned index = 0; index != 4; ++index) {
    char byte;
    ssize_t count = __llvm_libc_stdio_read(cookie(stream), &byte, 1);
    if (count != 1) {
      if (index != 0) {
        libc_errno = EILSEQ;
        __llvm_libc_mmix_stream_set_error(cookie(stream));
      }
      return WEOF;
    }
    auto decoded = internal::mbrtowc(&wc, &byte, 1, &state);
    if (!decoded.has_value()) {
      libc_errno = decoded.error();
      __llvm_libc_mmix_stream_set_error(cookie(stream));
      return WEOF;
    }
    if (decoded.value() != static_cast<size_t>(-2))
      return static_cast<wint_t>(wc);
  }

  libc_errno = EILSEQ;
  __llvm_libc_mmix_stream_set_error(cookie(stream));
  return WEOF;
}

} // namespace

LLVM_LIBC_FUNCTION(int, fwide, (::FILE * stream, int mode)) {
  LIBC_CRASH_ON_NULLPTR(stream);
  return __llvm_libc_mmix_stream_orient(cookie(stream), mode);
}

LLVM_LIBC_FUNCTION(wint_t, fputwc, (wchar_t wc, ::FILE *stream)) {
  LIBC_CRASH_ON_NULLPTR(stream);
  return write_wide_character(wc, stream);
}

LLVM_LIBC_FUNCTION(wint_t, putwc, (wchar_t wc, ::FILE *stream)) {
  LIBC_CRASH_ON_NULLPTR(stream);
  return write_wide_character(wc, stream);
}

LLVM_LIBC_FUNCTION(wint_t, putwchar, (wchar_t wc)) {
  return write_wide_character(wc, standard_output());
}

LLVM_LIBC_FUNCTION(int, fputws,
                   (const wchar_t *__restrict str, ::FILE *__restrict stream)) {
  LIBC_CRASH_ON_NULLPTR(str);
  LIBC_CRASH_ON_NULLPTR(stream);
  while (*str != L'\0')
    if (write_wide_character(*str++, stream) == WEOF)
      return -1;
  return 0;
}

LLVM_LIBC_FUNCTION(wint_t, fgetwc, (::FILE * stream)) {
  LIBC_CRASH_ON_NULLPTR(stream);
  return read_wide_character(stream);
}

LLVM_LIBC_FUNCTION(wint_t, getwc, (::FILE * stream)) {
  LIBC_CRASH_ON_NULLPTR(stream);
  return read_wide_character(stream);
}

LLVM_LIBC_FUNCTION(wint_t, getwchar, ()) {
  return read_wide_character(standard_input());
}

LLVM_LIBC_FUNCTION(wchar_t *, fgetws,
                   (wchar_t *__restrict str, int count,
                    ::FILE *__restrict stream)) {
  if (count <= 0)
    return nullptr;
  LIBC_CRASH_ON_NULLPTR(str);
  LIBC_CRASH_ON_NULLPTR(stream);
  if (!orient_wide(stream)) {
    libc_errno = EINVAL;
    return nullptr;
  }
  if (count == 1) {
    str[0] = L'\0';
    return str;
  }

  int length = 0;
  while (length != count - 1) {
    wint_t value = read_wide_character(stream);
    if (value == WEOF) {
      if (length == 0)
        return nullptr;
      break;
    }
    str[length++] = static_cast<wchar_t>(value);
    if (value == L'\n')
      break;
  }
  str[length] = L'\0';
  return str;
}

LLVM_LIBC_FUNCTION(wint_t, ungetwc, (wint_t wc, ::FILE *stream)) {
  LIBC_CRASH_ON_NULLPTR(stream);
  if (wc == WEOF || !orient_wide(stream))
    return WEOF;

  char bytes[internal::MAX_LOCALE_CHARACTER_BYTES];
  internal::mbstate state{};
  auto encoded = internal::wcrtomb(bytes, static_cast<wchar_t>(wc), &state);
  if (!encoded.has_value()) {
    libc_errno = encoded.error();
    return WEOF;
  }
  if (__llvm_libc_mmix_stream_pushback(
          reinterpret_cast<const unsigned char *>(bytes),
          static_cast<unsigned>(encoded.value()), cookie(stream)) != 0)
    return WEOF;
  return wc;
}

} // namespace LIBC_NAMESPACE_DECL
