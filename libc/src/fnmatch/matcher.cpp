//===-- Locale-aware pathname pattern matching ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/fnmatch/matcher.h"
#include "hdr/errno_macros.h"
#include "hdr/fnmatch_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"
#include "src/locale/collation_key.h"

/*
 * Adapted from BSD fnmatch, as retained in newlib/libc/posix/fnmatch.c and
 * winsup/cygwin/libc/fnmatch.c (FreeBSD revision 288309). The bracket syntax
 * and pathname rules are retained; character handling and matching now use
 * libc's locale services.
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
namespace internal {
namespace {
using Scalars = cpp::span<const uint32_t>;

template <class T> class Storage {
  T *data = nullptr;

public:
  Storage() = default;
  Storage(const Storage &) = delete;
  Storage &operator=(const Storage &) = delete;
  ~Storage() { ::free(data); }
  int allocate(size_t count) {
    if (count > cpp::numeric_limits<size_t>::max() / sizeof(T))
      return EOVERFLOW;
    if (count == 0)
      return 0;
    AllocChecker ac;
    data = new (ac) T[count];
    return ac ? 0 : ENOMEM;
  }
  T *get() { return data; }
};

ErrorOr<size_t> decode(cpp::string_view source, RestartableConverter converter,
                       uint32_t *output) {
  mbstate state{};
  size_t count = 0;
  while (!source.empty()) {
    uint32_t scalar;
    auto read = converter.decode(
        &scalar,
        {reinterpret_cast<const uint8_t *>(source.data()), source.size()},
        state);
    if (!read)
      return Error(read.error());
    if (*read == size_t(-2)) {
      if (!converter.boundary(state))
        return Error(EILSEQ);
      break;
    }
    // Embedded NUL is not part of a pathname pattern/string.
    if (!*read || *read > source.size())
      return Error(EILSEQ);
    output[count++] = scalar;
    source.remove_prefix(*read);
  }
  return count;
}

enum class Kind { LITERAL, CLASS, EQUIVALENT, RANGE };
struct Item {
  Kind kind = Kind::LITERAL;
  Scalars first, last;
  wctype_t type = WCTYPE_INVALID;
};
enum class Op { LITERAL, ANY, STAR, BRACKET, NEVER };
struct Token {
  Op op = Op::LITERAL;
  uint32_t scalar = 0;
  size_t begin = 0, count = 0;
  bool negate = false;
};

class Matcher {
  Scalars pattern, input;
  int flags;
  const __locale_data *ctype;
  const CollationData *collation;
  Item *items;
  size_t used = 0;

  bool equal(uint32_t a, uint32_t b) const {
    return a == b ||
           ((flags & FNM_CASEFOLD) &&
            (map_wide(a, false, ctype) == map_wide(b, false, ctype) ||
             map_wide(a, true, ctype) == map_wide(b, true, ctype)));
  }
  bool equal(Scalars a, Scalars b) const {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i != a.size(); ++i)
      if (!equal(a[i], b[i]))
        return false;
    return true;
  }
  bool element(Scalars value) const {
    return value.size() == 1 ||
           (collation && value.size() <= CollationData::MAX_SEQUENCE &&
            collation->find(value).has_value());
  }
  int order(Scalars a, Scalars b, bool primary) const {
    if (!collation)
      return (a[0] > b[0]) - (a[0] < b[0]);
    auto ar = a.size() == 1 ? collation->lookup(a) : collation->find(a);
    auto br = b.size() == 1 ? collation->lookup(b) : collation->find(b);
    if (!ar || !br)
      return 2;
    if (!primary)
      return compare_collation({&*ar, 1}, {&*br, 1});
    auto aw = ar->weights(0).value(), bw = br->weights(0).value();
    size_t count = aw.size() < bw.size() ? aw.size() : bw.size();
    for (size_t i = 0; i != count; ++i) {
      uint32_t x = aw.at(i).value(), y = bw.at(i).value();
      if (x != y)
        return x < y ? -1 : 1;
    }
    return (aw.size() > bw.size()) - (aw.size() < bw.size());
  }
  bool ascii_name(Scalars value, cpp::string_view name) const {
    if (value.size() != name.size())
      return false;
    for (size_t i = 0; i != value.size(); ++i)
      if (value[i] != static_cast<unsigned char>(name[i]))
        return false;
    return true;
  }
  bool atom(size_t &pos, Item &item, bool &valid) const {
    size_t start = pos;
    if (pos >= pattern.size())
      return false;
    if (pattern[pos] == '\\' && !(flags & FNM_NOESCAPE)) {
      if (++pos == pattern.size())
        return false;
      start = pos++;
    } else if (pattern[pos] == '[' && pos + 1 < pattern.size() &&
               (pattern[pos + 1] == ':' || pattern[pos + 1] == '.' ||
                pattern[pos + 1] == '=')) {
      uint32_t kind = pattern[pos + 1];
      pos += 2;
      start = pos;
      while (pos + 1 < pattern.size() &&
             !(pattern[pos] == kind && pattern[pos + 1] == ']'))
        ++pos;
      if (pos + 1 == pattern.size() || pos == pattern.size())
        return false;
      item.first = pattern.subspan(start, pos - start);
      pos += 2;
      if (kind == ':') {
        item.kind = Kind::CLASS;
        for (const auto &mapping : mappings)
          if (ascii_name(item.first, mapping.name))
            item.type = mapping.desc;
        valid &= item.type != WCTYPE_INVALID;
      } else {
        item.kind = kind == '=' ? Kind::EQUIVALENT : Kind::LITERAL;
        valid &= element(item.first);
      }
      return true;
    } else {
      ++pos;
    }
    item.first = pattern.subspan(start, 1);
    return true;
  }
  bool bracket(size_t &pos, Token &token) {
    size_t p = pos + 1, old_used = used;
    bool valid = true;
    if (p != pattern.size() && (pattern[p] == '!' || pattern[p] == '^')) {
      token.negate = true;
      ++p;
    }
    size_t first = p;
    while (p < pattern.size() && (pattern[p] != ']' || p == first)) {
      Item item;
      if (!atom(p, item, valid))
        break;
      if (item.kind == Kind::LITERAL && p + 1 < pattern.size() &&
          pattern[p] == '-' && pattern[p + 1] != ']') {
        ++p;
        Item last;
        if (!atom(p, last, valid))
          break;
        valid &= last.kind == Kind::LITERAL;
        item.kind = Kind::RANGE;
        item.last = last.first;
      }
      if (flags & FNM_PATHNAME) {
        for (auto c : item.first)
          valid &= c != '/';
        for (auto c : item.last)
          valid &= c != '/';
      }
      items[used++] = item;
      if (p < pattern.size() && pattern[p] == ']') {
        token.op = valid ? Op::BRACKET : Op::NEVER;
        token.begin = old_used;
        token.count = used - old_used;
        pos = p + 1;
        return true;
      }
    }
    // Preserve the existing malformed-bracket literal policy.
    used = old_used;
    return false;
  }
  bool item_matches(const Item &item, Scalars value) const {
    switch (item.kind) {
    case Kind::LITERAL:
      return equal(item.first, value);
    case Kind::CLASS:
      return value.size() == 1 && classify_wide(value[0], item.type, ctype);
    case Kind::EQUIVALENT:
      return equal(item.first, value) ||
             (element(value) && order(item.first, value, true) == 0);
    case Kind::RANGE:
      if (!element(value))
        return false;
      if ((flags & FNM_CASEFOLD) &&
          order(item.first, item.last, false) <= 0 &&
          (equal(item.first, value) || equal(item.last, value)))
        return true;
      return order(item.first, value, false) <= 0 &&
             order(value, item.last, false) <= 0;
    }
    return false;
  }
  bool matches_with_case(const Item &item, Scalars value) const {
    if (item_matches(item, value))
      return true;
    if (!(flags & FNM_CASEFOLD) || item.kind == Kind::LITERAL)
      return false;
    // Preserve the original range/equivalence weights. Case-insensitive
    // matching adds counterparts; it must not replace the original set.
    uint32_t mapped[CollationData::MAX_SEQUENCE];
    for (unsigned upper = 0; upper != 2; ++upper) {
      for (size_t i = 0; i != value.size(); ++i)
        mapped[i] = map_wide(value[i], upper != 0, ctype);
      if (item_matches(item, {mapped, value.size()}))
        return true;
    }
    if (value.size() == 1) {
      struct Context {
        const Matcher *matcher;
        const Item *item;
      } context{this, &item};
      auto accept = [](wint_t source, void *data) {
        auto &context = *static_cast<Context *>(data);
        uint32_t scalar = source;
        return context.matcher->item_matches(*context.item, {&scalar, 1});
      };
      if (case_source_matches(value[0], false, ctype, accept, &context) ||
          case_source_matches(value[0], true, ctype, accept, &context))
        return true;
    }
    // Mixed-case counterparts of a multi-character element need not have
    // their own collation records. Enumerate records, not 2^length variants.
    if (collation && item.kind != Kind::CLASS) {
      for (size_t i = 0; i != collation->size(); ++i) {
        auto candidate = collation->rule(i).value();
        if (candidate.source_size() != value.size())
          continue;
        for (size_t j = 0; j != value.size(); ++j)
          mapped[j] = candidate.source(j);
        Scalars counterpart{mapped, value.size()};
        if (equal(value, counterpart) && item_matches(item, counterpart))
          return true;
      }
    }
    return false;
  }
  bool restricted(size_t index) const {
    return input[index] == '.' && (flags & FNM_PERIOD) &&
           (index == 0 || ((flags & FNM_PATHNAME) && input[index - 1] == '/'));
  }
  bool separator(size_t index) const {
    return input[index] == '/' && (flags & FNM_PATHNAME);
  }

public:
  Matcher(Scalars pattern, Scalars input, int flags, const __locale_data *ctype,
          const CollationData *collation, Item *items)
      : pattern(pattern), input(input), flags(flags),
        ctype(ctype ? ctype : current_ctype()), collation(collation),
        items(items) {}
  size_t compile(Token *tokens) {
    size_t count = 0;
    for (size_t pos = 0; pos < pattern.size();) {
      Token token;
      uint32_t c = pattern[pos];
      if (c == '[' && bracket(pos, token)) {
        tokens[count++] = token;
        continue;
      }
      ++pos;
      if (c == '*') {
        token.op = Op::STAR;
        while (pos != pattern.size() && pattern[pos] == '*')
          ++pos;
      } else if (c == '?') {
        token.op = Op::ANY;
      } else if (c == '\\' && !(flags & FNM_NOESCAPE)) {
        if (pos == pattern.size())
          token.op = Op::NEVER;
        else
          token.scalar = pattern[pos++];
      } else {
        token.scalar = c;
      }
      tokens[count++] = token;
    }
    return count;
  }
  bool run(cpp::span<const Token> tokens, bool *next, bool *current) const {
    for (size_t s = 0; s <= input.size(); ++s)
      next[s] =
          s == input.size() || ((flags & FNM_LEADING_DIR) && input[s] == '/');
    // Two dynamic-programming rows keep every multicharacter bracket choice
    // without recursion, input-sized call stacks or exponential star retries.
    for (size_t t = tokens.size(); t != 0; --t) {
      const Token &token = tokens[t - 1];
      for (size_t end = input.size() + 1; end != 0; --end) {
        size_t s = end - 1;
        bool matched = false;
        if (token.op == Op::STAR) {
          matched = (s == input.size() || !restricted(s)) &&
                    (next[s] ||
                     (s != input.size() && !separator(s) && current[s + 1]));
        } else if (s != input.size()) {
          if (token.op == Op::LITERAL)
            matched = equal(token.scalar, input[s]) && next[s + 1];
          else if (token.op == Op::ANY)
            matched = !restricted(s) && !separator(s) && next[s + 1];
          else if (token.op == Op::BRACKET && !restricted(s) && !separator(s)) {
            size_t limit = token.negate || !collation ? 1
                           : input.size() - s < CollationData::MAX_SEQUENCE
                               ? input.size() - s
                               : CollationData::MAX_SEQUENCE;
            for (size_t length = 1; length <= limit && !matched; ++length) {
              if (separator(s + length - 1))
                break;
              if (!next[s + length])
                continue;
              bool member = false;
              for (size_t i = 0; i != token.count && !member; ++i)
                member = matches_with_case(items[token.begin + i],
                                           input.subspan(s, length));
              matched = member != token.negate;
            }
          }
        }
        current[s] = matched;
      }
      bool *swap = next;
      next = current;
      current = swap;
    }
    return next[0];
  }
};
} // namespace

ErrorOr<bool> match_path_pattern(cpp::string_view pattern,
                                 cpp::string_view input, int flags,
                                 RestartableConverter converter,
                                 const __locale_data *ctype,
                                 const CollationData *collation) {
  Storage<uint32_t> pattern_scalars, input_scalars;
  if (int error = pattern_scalars.allocate(pattern.size()))
    return Error(error);
  if (int error = input_scalars.allocate(input.size()))
    return Error(error);
  auto p = decode(pattern, converter, pattern_scalars.get());
  if (!p)
    return Error(p.error());
  auto s = decode(input, converter, input_scalars.get());
  if (!s)
    return Error(s.error());
  Storage<Token> tokens;
  Storage<Item> items;
  Storage<bool> rows;
  if (int error = tokens.allocate(*p))
    return Error(error);
  if (int error = items.allocate(*p))
    return Error(error);
  if (*s >= cpp::numeric_limits<size_t>::max() / 2)
    return Error(EOVERFLOW);
  if (int error = rows.allocate(2 * (*s + 1)))
    return Error(error);
  Matcher matcher({pattern_scalars.get(), *p}, {input_scalars.get(), *s}, flags,
                  ctype, collation, items.get());
  size_t count = matcher.compile(tokens.get());
  return matcher.run({tokens.get(), count}, rows.get(), rows.get() + *s + 1);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
