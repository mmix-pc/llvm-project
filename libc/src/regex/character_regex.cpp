//===-- Character-oriented BSD regex composition --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/regex/character_regex.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"
#include "src/locale/character.h"
#include "src/regex/locale_bracket.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
class Scalars {
  llvm_regex_char_t *storage = nullptr;
  const llvm_regex_char_t sentinel = 0;

public:
  Scalars() = default;
  Scalars(const Scalars &) = delete;
  Scalars &operator=(const Scalars &) = delete;
  ~Scalars() { ::free(storage); }
  bool assign(const DecodedRegexText &text) {
    auto units = text.units();
    if (units.empty())
      return true;
    if (units.size() >=
        cpp::numeric_limits<size_t>::max() / sizeof(llvm_regex_char_t))
      return false;
    AllocChecker ac;
    storage = new (ac) llvm_regex_char_t[units.size() + 1];
    if (!ac)
      return false;
    for (size_t i = 0; i != units.size(); ++i)
      storage[i] = static_cast<llvm_regex_char_t>(units[i].scalar);
    storage[units.size()] = 0;
    return true;
  }
  const llvm_regex_char_t *data() const {
    return storage ? storage : &sentinel;
  }
};

int decode_error(int error) {
  return error == ENOMEM || error == EOVERFLOW ? REG_ESPACE : REG_BADPAT;
}

int classify(const void *context, unsigned type, llvm_regex_char_t c) {
  constexpr wctype_t types[] = {WCTYPE_ALNUM, WCTYPE_ALPHA, WCTYPE_BLANK,
                                WCTYPE_CNTRL, WCTYPE_DIGIT, WCTYPE_GRAPH,
                                WCTYPE_LOWER, WCTYPE_PRINT, WCTYPE_PUNCT,
                                WCTYPE_SPACE, WCTYPE_UPPER, WCTYPE_XDIGIT};
  if (type > LLVM_REGEX_XDIGIT)
    return 0;
  return classify_wide(static_cast<wint_t>(c), types[type],
                       static_cast<const __locale_data *>(context));
}

llvm_regex_char_t map(const void *context, llvm_regex_char_t c, int upper) {
  return static_cast<llvm_regex_char_t>(
      map_wide(static_cast<wint_t>(c), upper != 0,
               static_cast<const __locale_data *>(context)));
}

bool valid_match(llvm_regmatch_t match, size_t size) {
  if (match.rm_so == -1)
    return match.rm_eo == -1;
  return match.rm_so >= 0 && match.rm_eo >= match.rm_so &&
         static_cast<size_t>(match.rm_eo) <= size;
}

regmatch_t bytes(const DecodedRegexText &input, llvm_regmatch_t match) {
  if (match.rm_so == -1)
    return {-1, -1};
  auto units = input.units();
  if (match.rm_so == match.rm_eo) {
    regoff_t offset = static_cast<size_t>(match.rm_so) == units.size()
                          ? input.bytes()
                          : units[match.rm_so].begin;
    return {offset, offset};
  }
  return {units[match.rm_so].begin, units[match.rm_eo - 1].end};
}
} // namespace

CharacterRegex::~CharacterRegex() { __llvm_libc_bsd_regfree(&engine); }

int CharacterRegex::compile(cpp::string_view pattern, int flags,
                            RestartableConverter converter,
                            const __locale_data *ctype,
                            const CollationData *collation) {
  if (flags & ~(REG_EXTENDED | REG_ICASE | REG_NOSUB | REG_NEWLINE))
    return REG_BADPAT;
  DecodedRegexText text;
  if (int error = text.assign(pattern, converter))
    return decode_error(error);
  Scalars characters;
  if (!characters.assign(text))
    return REG_ESPACE;
  llvm_regex_t next{};
  const llvm_regex_ctype services{ctype, classify, map};
  const RegexBracketLocale locale{ctype, collation, flags};
  const llvm_regex_bracket_compiler brackets{&locale, compile_regex_bracket};
  if (!text.units().empty()) {
    if (int error = __llvm_libc_bsd_regcomp_chars_l(
            &next, characters.data(), text.units().size(), flags,
            ctype ? &services : nullptr,
            (ctype || collation) ? &brackets : nullptr))
      return error;
  }
  __llvm_libc_bsd_regfree(&engine);
  engine = next;
  ready = true;
  empty = text.units().empty();
  nosub = (flags & REG_NOSUB) != 0;
  return 0;
}

int CharacterRegex::execute(cpp::string_view input, size_t count,
                            regmatch_t *matches, int flags,
                            RestartableConverter converter) const {
  if (!ready || (flags & ~(REG_NOTBOL | REG_NOTEOL)))
    return REG_BADPAT;
  if (nosub)
    count = 0;
  DecodedRegexText text;
  if (int error = text.assign(input, converter))
    return decode_error(error);
  Scalars characters;
  if (!characters.assign(text))
    return REG_ESPACE;
  size_t used = count < engine.re_nsub + 1 ? count : engine.re_nsub + 1;
  struct Matches {
    llvm_regmatch_t *storage = nullptr;
    ~Matches() { ::free(storage); }
  } positions;
  if (used) {
    if (used > cpp::numeric_limits<size_t>::max() / sizeof(llvm_regmatch_t))
      return REG_ESPACE;
    AllocChecker ac;
    positions.storage = new (ac) llvm_regmatch_t[used];
    if (!ac)
      return REG_ESPACE;
  }
  if (empty) {
    if (used)
      positions.storage[0] = {0, 0};
  } else if (int error = __llvm_libc_bsd_regexec_chars(
                 &engine, characters.data(), text.units().size(), used,
                 positions.storage, flags)) {
    return error;
  }
  for (size_t i = 0; i != used; ++i)
    if (!valid_match(positions.storage[i], text.units().size()))
      return REG_BADPAT;
  for (size_t i = 0; i != count; ++i)
    matches[i] =
        i < used ? bytes(text, positions.storage[i]) : regmatch_t{-1, -1};
  return 0;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
