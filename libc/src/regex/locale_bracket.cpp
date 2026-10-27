//===-- Locale bracket compiler for the private regex engine --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/regex/locale_bracket.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"
#include "src/locale/character.h"
#include "src/locale/collation_key.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using Scalars = cpp::span<const uint32_t>;
enum class Kind { LITERAL, CLASS, EQUIVALENT, RANGE };
struct Item {
  Kind kind = Kind::LITERAL;
  Scalars first, last;
  wctype_t type = WCTYPE_INVALID;
};

class Bracket {
  RegexBracketLocale locale;
  uint32_t *storage = nullptr;
  Item *items = nullptr;
  size_t count = 0;
  bool negate = false;

  bool element(Scalars value) const {
    return value.size() == 1 || (locale.collation && !value.empty() &&
                                 value.size() <= CollationData::MAX_SEQUENCE &&
                                 locale.collation->find(value).has_value());
  }
  bool equal(Scalars a, Scalars b) const {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i != a.size(); ++i) {
      if (a[i] == b[i])
        continue;
      if (!(locale.flags & REG_ICASE) ||
          (map_wide(a[i], false, locale.ctype) !=
               map_wide(b[i], false, locale.ctype) &&
           map_wide(a[i], true, locale.ctype) !=
               map_wide(b[i], true, locale.ctype)))
        return false;
    }
    return true;
  }
  int order(Scalars a, Scalars b, bool primary) const {
    if (!locale.collation)
      return (a[0] > b[0]) - (a[0] < b[0]);
    auto ar =
        a.size() == 1 ? locale.collation->lookup(a) : locale.collation->find(a);
    auto br =
        b.size() == 1 ? locale.collation->lookup(b) : locale.collation->find(b);
    // Callers validate all operands as elements before comparison.
    if (!primary)
      return compare_collation({&ar.value(), 1}, {&br.value(), 1});
    auto aw = ar->weights(0).value(), bw = br->weights(0).value();
    size_t n = aw.size() < bw.size() ? aw.size() : bw.size();
    for (size_t i = 0; i != n; ++i) {
      auto x = aw.at(i).value(), y = bw.at(i).value();
      if (x != y)
        return x < y ? -1 : 1;
    }
    return (aw.size() > bw.size()) - (aw.size() < bw.size());
  }
  int atom(Scalars text, size_t &pos, Item &item) const {
    if (pos >= text.size())
      return REG_EBRACK;
    size_t start = pos++;
    if (text[start] != '[' || pos == text.size() ||
        (text[pos] != ':' && text[pos] != '.' && text[pos] != '=')) {
      item.first = text.subspan(start, 1);
      return 0;
    }
    uint32_t kind = text[pos++];
    start = pos;
    while (pos + 1 < text.size() &&
           !(text[pos] == kind && text[pos + 1] == ']'))
      ++pos;
    if (pos + 1 >= text.size())
      return REG_EBRACK;
    item.first = text.subspan(start, pos - start);
    pos += 2;
    if (kind != ':') {
      item.kind = kind == '=' ? Kind::EQUIVALENT : Kind::LITERAL;
      return element(item.first) ? 0 : REG_ECOLLATE;
    }
    item.kind = Kind::CLASS;
    for (const auto &mapping : mappings) {
      bool same = item.first.size() == mapping.name.size();
      for (size_t i = 0; same && i != item.first.size(); ++i)
        same = item.first[i] == static_cast<unsigned char>(mapping.name[i]);
      if (same) {
        item.type = mapping.desc;
        return 0;
      }
    }
    return REG_ECTYPE;
  }
  bool matches_item(const Item &item, Scalars value) const {
    switch (item.kind) {
    case Kind::LITERAL:
      return equal(item.first, value);
    case Kind::CLASS:
      return value.size() == 1 &&
             classify_wide(value[0], item.type, locale.ctype);
    case Kind::EQUIVALENT:
      return equal(item.first, value) || order(item.first, value, true) == 0;
    case Kind::RANGE:
      return order(item.first, value, false) <= 0 &&
             order(value, item.last, false) <= 0;
    }
    return false;
  }
  bool matches(Scalars value) const {
    for (size_t i = 0; i != count; ++i) {
      if (matches_item(items[i], value))
        return true;
      if (!(locale.flags & REG_ICASE))
        continue;
      for (unsigned mode = 0; mode != 2; ++mode) {
        uint32_t mapped[CollationData::MAX_SEQUENCE];
        for (size_t j = 0; j != value.size(); ++j)
          mapped[j] = static_cast<uint32_t>(
              map_wide(value[j], mode != 0, locale.ctype));
        Scalars variant{mapped, value.size()};
        if (element(variant) && matches_item(items[i], variant))
          return true;
      }
      if (value.size() == 1) {
        struct Context {
          const Bracket *bracket;
          const Item *item;
        } context{this, &items[i]};
        auto accept = [](wint_t source, void *data) {
          auto &context = *static_cast<Context *>(data);
          uint32_t scalar = source;
          return context.bracket->matches_item(*context.item, {&scalar, 1});
        };
        if (case_source_matches(value[0], false, locale.ctype, accept, &context) ||
            case_source_matches(value[0], true, locale.ctype, accept, &context))
          return true;
      }
    }
    return false;
  }

public:
  explicit Bracket(RegexBracketLocale locale) : locale(locale) {}
  ~Bracket() {
    ::free(items);
    ::free(storage);
  }
  int compile(const llvm_regex_char_t *input, size_t size, size_t *consumed) {
    if (!size)
      return REG_EBRACK;
    // Retain only this bracket, not the remaining expression for every set.
    size_t end = input[0] == '^' ? 1 : 0;
    size_t first_character = end;
    for (; end < size; ++end) {
      if (input[end] == ']' && end != first_character)
        break;
      if (input[end] != '[' || end + 1 == size ||
          (input[end + 1] != ':' && input[end + 1] != '.' &&
           input[end + 1] != '='))
        continue;
      auto marker = input[end + 1];
      end += 2;
      while (end + 1 < size && !(input[end] == marker && input[end + 1] == ']'))
        ++end;
      if (end + 1 >= size)
        return REG_EBRACK;
      ++end;
    }
    if (end == size)
      return REG_EBRACK;
    size = end + 1;
    if (size > cpp::numeric_limits<size_t>::max() / sizeof(Item) ||
        size > cpp::numeric_limits<size_t>::max() / sizeof(uint32_t))
      return REG_ESPACE;
    AllocChecker ac;
    storage = new (ac) uint32_t[size];
    if (!ac)
      return REG_ESPACE;
    for (size_t i = 0; i != size; ++i)
      storage[i] = static_cast<uint32_t>(input[i]);
    items = new (ac) Item[size];
    if (!ac)
      return REG_ESPACE;
    Scalars text{storage, size};
    size_t pos = 0;
    if (text[pos] == '^') {
      negate = true;
      ++pos;
    }
    size_t first = pos;
    while (pos < size && (text[pos] != ']' || pos == first)) {
      Item item;
      if (int error = atom(text, pos, item))
        return error;
      if (pos + 1 < size && text[pos] == '-' && text[pos + 1] != ']') {
        if (item.kind != Kind::LITERAL)
          return REG_ERANGE;
        ++pos;
        Item last;
        if (int error = atom(text, pos, last))
          return error;
        if (last.kind != Kind::LITERAL ||
            order(item.first, last.first, false) > 0)
          return REG_ERANGE;
        item.kind = Kind::RANGE;
        item.last = last.first;
      }
      items[count++] = item;
    }
    if (pos == size)
      return REG_EBRACK;
    *consumed = pos + 1;
    return 0;
  }
  int contains(llvm_regex_char_t character) const {
    uint32_t scalar = static_cast<uint32_t>(character);
    if (negate && (locale.flags & REG_NEWLINE) && scalar == '\n')
      return 0;
    return matches({&scalar, 1}) != negate;
  }
  int sequences(void *context,
                int (*emit)(void *, const llvm_regex_char_t *, size_t)) const {
    // Negative lists consume one character, not a collating sequence.
    if (negate || !locale.collation)
      return 0;
    bool can_match_sequence = false;
    for (size_t i = 0; i != count; ++i)
      can_match_sequence |=
          items[i].kind == Kind::EQUIVALENT || items[i].kind == Kind::RANGE ||
          (items[i].kind == Kind::LITERAL && items[i].first.size() > 1);
    if (!can_match_sequence)
      return 0;
    for (size_t i = 0; i != locale.collation->size(); ++i) {
      auto rule = locale.collation->rule(i).value();
      if (rule.source_size() < 2)
        continue;
      uint32_t scalars[CollationData::MAX_SEQUENCE];
      llvm_regex_char_t output[CollationData::MAX_SEQUENCE];
      for (size_t j = 0; j != rule.source_size(); ++j) {
        scalars[j] = rule.source(j);
        output[j] = static_cast<llvm_regex_char_t>(scalars[j]);
      }
      if (matches({scalars, rule.source_size()}))
        if (int error = emit(context, output, rule.source_size()))
          return error;
    }
    return 0;
  }
};
void destroy(void *context) {
  static_cast<Bracket *>(context)->~Bracket();
  ::free(context);
}
} // namespace

int compile_regex_bracket(const void *context, const llvm_regex_char_t *text,
                          size_t size, size_t *consumed,
                          llvm_regex_bracket *result) {
  AllocChecker ac;
  Bracket *bracket =
      new (ac) Bracket(*static_cast<const RegexBracketLocale *>(context));
  if (!ac)
    return REG_ESPACE;
  if (int error = bracket->compile(text, size, consumed)) {
    destroy(bracket);
    return error;
  }
  *result = {bracket,
             [](const void *p, llvm_regex_char_t c) {
               return static_cast<const Bracket *>(p)->contains(c);
             },
             [](const void *p, void *out,
                int (*emit)(void *, const llvm_regex_char_t *, size_t)) {
               return static_cast<const Bracket *>(p)->sequences(out, emit);
             },
             destroy};
  return 0;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
