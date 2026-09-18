//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of the fnmatch function.
///
//===----------------------------------------------------------------------===//

#include "src/fnmatch/fnmatch.h"
#include "hdr/fnmatch_macros.h"
#include "src/__support/common.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/macros/config.h"
#include "src/__support/wctype_impl.h"

/*
 * Adapted from BSD fnmatch, as retained in newlib/libc/posix/fnmatch.c and
 * winsup/cygwin/libc/fnmatch.c (FreeBSD revision 288309). The iterative star
 * backtracking is retained; character handling uses libc's C-locale services.
 *
 * Copyright (c) 1989, 1993, 1994
 * The Regents of the University of California. All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guido van Rossum.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

namespace LIBC_NAMESPACE_DECL {
namespace {

enum class RangeMatch { Match, NoMatch, Malformed };

unsigned char fold(unsigned char c, int flags) {
  return flags & FNM_CASEFOLD ? static_cast<unsigned char>(
                                    internal::tolower(static_cast<char>(c)))
                              : c;
}

// FIXME: Add locale-aware decoding and collation when libc supports them.
// In the C locale, collating elements and equivalence classes are single bytes.
RangeMatch rangematch(const char *pattern, unsigned char test, int flags,
                      const char *&end) {
  bool negate = *pattern == '!' || *pattern == '^';
  if (negate)
    ++pattern;
  const char *first = pattern;
  bool matched = false;
  bool invalid = false;
  while (*pattern != ']' || pattern == first) {
    if (!*pattern)
      return RangeMatch::Malformed;
    unsigned char c = static_cast<unsigned char>(*pattern++);
    if (c == '\\' && !(flags & FNM_NOESCAPE)) {
      if (!*pattern)
        return RangeMatch::Malformed;
      c = static_cast<unsigned char>(*pattern++);
    } else if (c == '[' &&
               (*pattern == ':' || *pattern == '.' || *pattern == '=')) {
      char kind = *pattern++;
      const char *name = pattern;
      while (*pattern && !(*pattern == kind && pattern[1] == ']'))
        ++pattern;
      if (!*pattern)
        return RangeMatch::Malformed;
      cpp::string_view value(name, static_cast<size_t>(pattern - name));
      pattern += 2;
      if (kind == ':') {
        wctype_t desc = internal::WCTYPE_INVALID;
        for (const auto &mapping : internal::mappings)
          if (mapping.name == value)
            desc = mapping.desc;
        invalid |= desc == internal::WCTYPE_INVALID;
        matched |=
            test < 128 && internal::iswctype(static_cast<wchar_t>(test), desc);
        continue;
      }
      if (value.size() != 1) {
        invalid = true;
        continue;
      }
      c = static_cast<unsigned char>(value[0]);
      if (kind == '=') {
        matched |= fold(c, flags) == fold(test, flags);
        continue;
      }
    }

    if (c == '/' && (flags & FNM_PATHNAME))
      invalid = true;
    if (*pattern == '-' && pattern[1] && pattern[1] != ']') {
      ++pattern;
      unsigned char c2 = static_cast<unsigned char>(*pattern++);
      if (c2 == '\\' && !(flags & FNM_NOESCAPE)) {
        if (!*pattern)
          return RangeMatch::Malformed;
        c2 = static_cast<unsigned char>(*pattern++);
      } else if (c2 == '[' && *pattern == '.') {
        ++pattern;
        if (!*pattern)
          return RangeMatch::Malformed;
        c2 = static_cast<unsigned char>(*pattern++);
        if (*pattern != '.' || pattern[1] != ']')
          return RangeMatch::NoMatch;
        pattern += 2;
      }
      if (c2 == '/' && (flags & FNM_PATHNAME))
        invalid = true;
      matched |= fold(c, flags) <= fold(test, flags) &&
                 fold(test, flags) <= fold(c2, flags);
    } else {
      matched |= fold(c, flags) == fold(test, flags);
    }
  }
  end = pattern + 1;
  return !invalid && matched != negate ? RangeMatch::Match
                                       : RangeMatch::NoMatch;
}

} // namespace

LLVM_LIBC_FUNCTION(int, fnmatch,
                   (const char *pattern, const char *string, int flags)) {
  const char *stringstart = string;
  const char *bt_pattern = nullptr;
  const char *bt_string = nullptr;
  for (;;) {
    unsigned char c = static_cast<unsigned char>(*pattern++);
    unsigned char test = static_cast<unsigned char>(*string);
    bool leading_period = test == '.' && (flags & FNM_PERIOD) &&
                          (string == stringstart ||
                           ((flags & FNM_PATHNAME) && string[-1] == '/'));
    switch (c) {
    case '\0':
      if (!test || ((flags & FNM_LEADING_DIR) && test == '/'))
        return 0;
      goto backtrack;
    case '?':
      if (!test || leading_period || (test == '/' && (flags & FNM_PATHNAME)))
        goto backtrack;
      ++string;
      break;
    case '*':
      while (*pattern == '*')
        ++pattern;
      if (leading_period)
        goto backtrack;
      // Only the latest star needs retrying: an earlier star cannot improve
      // an already matched prefix. No recursion or input-sized storage is used.
      bt_pattern = pattern;
      bt_string = string;
      break;
    case '[': {
      if (!test || leading_period || (test == '/' && (flags & FNM_PATHNAME)))
        goto backtrack;
      const char *end = nullptr;
      switch (rangematch(pattern, test, flags, end)) {
      case RangeMatch::Match:
        pattern = end;
        ++string;
        break;
      case RangeMatch::NoMatch:
        goto backtrack;
      case RangeMatch::Malformed:
        goto literal;
      }
      break;
    }
    case '\\':
      if (!(flags & FNM_NOESCAPE)) {
        if (!*pattern)
          return FNM_NOMATCH;
        c = static_cast<unsigned char>(*pattern++);
      }
      [[fallthrough]];
    default:
    literal:
      if (!test || fold(c, flags) != fold(test, flags))
        goto backtrack;
      ++string;
      break;
    }
    continue;

  backtrack:
    if (!bt_pattern || !*bt_string ||
        (*bt_string == '/' && (flags & FNM_PATHNAME)))
      return FNM_NOMATCH;
    pattern = bt_pattern;
    string = ++bt_string;
  }
}

} // namespace LIBC_NAMESPACE_DECL
