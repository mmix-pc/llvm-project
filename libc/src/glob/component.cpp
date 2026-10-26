//===-- Locale-aware glob component parsing -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/glob/component.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<GlobComponent> prepare_glob_component(char *component, size_t length,
                                              bool noescape,
                                              bool followed_slash,
                                              RestartableConverter converter) {
  bool magic = false;
  // Pass one validates and detects syntax. Pass two removes escapes only if
  // the original component can be looked up without enumerating a directory.
  for (unsigned pass = 0; pass != 2; ++pass) {
    mbstate state{};
    size_t at = 0, written = 0, escape_at = 0;
    bool escaped = false;
    enum { OUTSIDE, FIRST, AFTER_NEGATION, INSIDE } bracket = OUTSIDE;
    while (at < length) {
      uint32_t scalar;
      auto read = converter.decode(
          &scalar,
          {reinterpret_cast<const uint8_t *>(component + at), length - at},
          state);
      if (!read)
        return Error(read.error());
      if (*read == size_t(-2)) {
        if (!converter.boundary(state))
          return Error(EILSEQ);
        if (pass)
          while (at != length)
            component[written++] = component[at++];
        break;
      }
      if (!*read || *read > length - at)
        return Error(EILSEQ);
      bool quote = !noescape && !escaped && scalar == '\\';
      if (quote)
        escape_at = at + *read - 1;
      if (!pass && !escaped && !quote) {
        if (scalar == '*' || scalar == '?')
          magic = true;
        if (bracket == OUTSIDE) {
          if (scalar == '[')
            bracket = FIRST;
        } else if (bracket == FIRST && (scalar == '!' || scalar == '^')) {
          bracket = AFTER_NEGATION;
        } else if (bracket == FIRST || bracket == AFTER_NEGATION) {
          bracket = INSIDE;
        } else if (scalar == ']') {
          magic = true;
        }
      } else if (!pass && escaped && bracket != OUTSIDE) {
        bracket = INSIDE;
      }
      if (pass) {
        size_t copy = *read;
        // An ASCII escape is the last byte of this decoded unit; any leading
        // designation/reset bytes still belong to the original filename.
        if (quote && (at + *read < length || followed_slash))
          --copy;
        for (size_t i = 0; i != copy; ++i)
          component[written++] = component[at + i];
      }
      escaped = quote;
      at += *read;
    }
    if (!pass && magic) {
      if (escaped && followed_slash) {
        for (size_t i = escape_at; i < length; ++i)
          component[i] = component[i + 1];
        --length;
      }
      return GlobComponent{true, length};
    }
    if (pass) {
      component[written] = '\0';
      return GlobComponent{false, written};
    }
  }
  __builtin_unreachable();
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
