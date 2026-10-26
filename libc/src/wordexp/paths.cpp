//===-- Word expansion field and pathname composition ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/paths.h"
#include "hdr/errno_macros.h"
#include "hdr/glob_macros.h"
#include "src/__support/libc_errno.h"
#include "src/glob/glob.h"
#include "src/glob/globfree.h"
#include "src/wordexp/fragments.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

int append(CharVector &output, cpp::string_view bytes) {
  for (char c : bytes)
    if (!output.append(c))
      return ENOMEM;
  return 0;
}

struct Matches {
  glob_t value{};
  ~Matches() { LIBC_NAMESPACE::globfree(&value); }
};

struct Paths {
  RestartableConverter converter;
  WordResults &results;
  WordFragments field;

  int finish() {
    CharVector raw, quoted;
    bool possible_pattern = false;
    for (const auto &part : field.parts()) {
      if (int error = append(raw, part.bytes))
        return error;
      for (char c : part.bytes) {
        if (!quoted.append(part.quoted))
          return ENOMEM;
        possible_pattern |= !part.quoted && (c == '*' || c == '?' || c == '[');
      }
    }
    auto original = cpp::string_view(raw.c_str(), raw.length());
    if (!possible_pattern)
      return results.append(original);

    CharVector pattern;
    mbstate state{};
    bool magic = false;
    size_t at = 0;
    while (at < original.size()) {
      uint32_t scalar;
      auto length = converter.decode(
          &scalar,
          {reinterpret_cast<const uint8_t *>(original.data() + at),
           original.size() - at},
          state);
      if (!length)
        return length.error();
      if (*length == size_t(-2)) {
        if (!converter.boundary(state))
          return EILSEQ;
        if (int error = append(pattern, original.substr(at)))
          return error;
        break;
      }
      if (!*length || *length > original.size() - at || !scalar)
        return EILSEQ;
      size_t end = at + *length;
      bool protect = quoted.c_str()[end - 1];
      magic |= !protect && (scalar == '*' || scalar == '?' || scalar == '[');
      bool escape =
          scalar == '\\' || (protect && scalar < 128 && scalar != '/');
      // ASCII syntax is the last byte of a decoded unit. Preserve any leading
      // state reset/designation bytes; do not re-encode filesystem names.
      if (escape) {
        if (int error = append(pattern, original.substr(at, *length - 1)))
          return error;
        if (!pattern.append('\\') || !pattern.append(original[end - 1]))
          return ENOMEM;
      } else if (int error = append(pattern, original.substr(at, *length))) {
        return error;
      }
      at = end;
    }
    if (!magic)
      return results.append(original);
    Matches matches;
    // NOCHECK would return the internally escaped pattern, not the original
    // field. Handle NOMATCH explicitly instead.
    int status =
        LIBC_NAMESPACE::glob(pattern.c_str(), 0, nullptr, &matches.value);
    if (status == GLOB_NOMATCH)
      return results.append(original);
    int error = status == GLOB_NOSPACE ? ENOMEM : status ? int(libc_errno) : 0;
    if (status && !error)
      error = EIO;
    if (!status || status == GLOB_NOSPACE)
      for (size_t i = 0; i < matches.value.gl_pathc; ++i)
        if (int failed = results.append(matches.value.gl_pathv[i]))
          return failed;
    return error;
  }

  static int add(void *p, cpp::string_view bytes, bool quoted) {
    return static_cast<Paths *>(p)->field.append(bytes, false, quoted);
  }
  static int complete(void *p) {
    auto &self = *static_cast<Paths *>(p);
    int error = self.finish();
    self.field.clear();
    return error;
  }
};
} // namespace

int append_word_paths(cpp::span<const ExpansionPart> parts,
                      cpp::span<const FieldDelimiter> delimiters,
                      RestartableConverter converter, WordResults &results) {
  Paths paths{converter, results, {}};
  auto split = split_word_fields(parts, delimiters,
                                 {&paths, Paths::add, Paths::complete});
  return split ? 0 : split.error();
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
